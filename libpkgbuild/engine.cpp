#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/process.hpp>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace pkgbuild {
namespace {

std::filesystem::path normalized(const std::filesystem::path& path)
{
    if (path.empty())
        throw Error(ErrorCode::invalid_configuration,
                    "build path must not be empty");
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

bool is_root_path(const std::filesystem::path& path)
{
    return path == path.root_path();
}

void reject_equal_path(const std::filesystem::path& work_base,
                       const std::filesystem::path& other,
                       const char* description)
{
    if (work_base == normalized(other))
        throw Error(ErrorCode::invalid_configuration,
                    "work directory base must differ from " +
                        std::string(description));
}

std::filesystem::path prepare_work_base(const BuildPaths& paths)
{
    auto base = std::filesystem::absolute(paths.work_dir).lexically_normal();
    if (base.empty() || is_root_path(base))
        throw Error(ErrorCode::invalid_configuration,
                    "work directory base must not be the filesystem root");

    const auto before = std::filesystem::symlink_status(base);
    if (std::filesystem::is_symlink(before))
        throw Error(ErrorCode::invalid_configuration,
                    "work directory base must not be a symbolic link");

    std::filesystem::create_directories(base);
    const auto after = std::filesystem::symlink_status(base);
    if (!std::filesystem::is_directory(after) ||
        std::filesystem::is_symlink(after))
        throw Error(ErrorCode::invalid_configuration,
                    "work directory base is not a real directory");

    base = normalized(base);
    if (is_root_path(base))
        throw Error(ErrorCode::invalid_configuration,
                    "work directory base must not resolve to the filesystem root");

    reject_equal_path(base, paths.recipe_dir, "the recipe directory");
    reject_equal_path(base, paths.source_dir, "the source directory");
    reject_equal_path(base, paths.package_dir, "the package directory");
    return base;
}

class PrivateWorkspace final {
public:
    PrivateWorkspace(const BuildPaths& paths, bool keep)
        : path_(create(prepare_work_base(paths))), keep_(keep) {}

    ~PrivateWorkspace()
    {
        if (!keep_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    static std::filesystem::path create(const std::filesystem::path& base)
    {
        std::string pattern = (base / ".pkgbuild.XXXXXX").string();
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');

        char* created = mkdtemp(storage.data());
        if (created == nullptr)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot create private build workspace in " +
                            base.string() + ": " + std::strerror(errno));

        const std::filesystem::path result(created);
        if (chmod(result.c_str(), 0700) != 0) {
            const int saved = errno;
            std::error_code ignored;
            std::filesystem::remove_all(result, ignored);
            throw Error(ErrorCode::filesystem_failed,
                        "cannot protect private build workspace: " +
                            std::string(std::strerror(saved)));
        }
        return result;
    }

    std::filesystem::path path_;
    bool keep_;
};

std::filesystem::path local_source_path(const Source& source,
                                        const BuildPaths& paths)
{
    if (source.uri)
        return std::filesystem::absolute(paths.source_dir / source.local_name);
    return std::filesystem::absolute(paths.recipe_dir / source.local_name);
}

void change_owner(const std::filesystem::path& path,
                  const BuildIdentity& identity)
{
    if (lchown(path.c_str(), identity.uid, identity.gid) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot assign build workspace ownership for " +
                        path.string() + ": " + std::strerror(errno));
}

void assign_workspace(const std::filesystem::path& root,
                      const std::optional<BuildIdentity>& identity)
{
    if (!identity || geteuid() != 0)
        return;

    change_owner(root, *identity);
    for (std::filesystem::recursive_directory_iterator iterator(root), end;
         iterator != end; ++iterator)
        change_owner(iterator->path(), *identity);
}

void prepare_metadata_directory(
    const std::filesystem::path& root,
    const std::optional<BuildIdentity>& identity)
{
    const auto metadata = root / "metadata";
    std::filesystem::create_directory(metadata);
    const mode_t mode = identity && geteuid() == 0 ? 0711 : 0700;
    if (chmod(metadata.c_str(), mode) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot protect build metadata directory: " +
                        std::string(std::strerror(errno)));
    if (identity && geteuid() == 0 && lchown(metadata.c_str(), 0, 0) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot retain build metadata directory ownership: " +
                        std::string(std::strerror(errno)));
}

} // namespace

PackageDefinition Engine::inspect(const DefinitionRequest& request,
                                  EventSink& events) const
{
    validate_execution_policy(request.execution);
    return services_.definitions.load(request, events);
}

BuildReceipt Engine::build(const BuildRequest& request,
                           EventSink& events) const
{
    validate_execution_policy(request.definition.execution);

    const auto& configured_paths = request.definition.paths;
    std::filesystem::create_directories(configured_paths.source_dir);
    std::filesystem::create_directories(configured_paths.package_dir);

    PrivateWorkspace workspace(configured_paths, request.keep_work);
    BuildPaths paths = configured_paths;
    paths.work_dir = workspace.path();

    std::filesystem::create_directories(paths.work_dir / "src");
    std::filesystem::create_directories(paths.work_dir / "pkg");
    std::filesystem::create_directories(paths.work_dir / "tmp");
    assign_workspace(paths.work_dir, request.definition.execution.identity);
    prepare_metadata_directory(paths.work_dir,
                               request.definition.execution.identity);

    DefinitionRequest definition_request = request.definition;
    definition_request.paths = paths;
    const auto definition = inspect(definition_request, events);

    const auto source_root = std::filesystem::absolute(paths.work_dir / "src");
    const auto package_root = std::filesystem::absolute(paths.work_dir / "pkg");

    BuildReceipt receipt;
    receipt.definition = definition;
    if (request.keep_work)
        receipt.work_directory = workspace.path();

    for (const auto& source : definition.sources) {
        auto local = local_source_path(source, paths);
        if (!std::filesystem::exists(local)) {
            if (!source.uri)
                throw Error(ErrorCode::missing_source,
                            "local source not found: " + local.string());
            if (!request.download_missing)
                throw Error(ErrorCode::missing_source,
                            "source not found; enable downloading: " +
                                local.string());

            receipt.downloads.push_back(
                services_.downloader.fetch(
                    DownloadRequest{*source.uri, local}, events));
        }

        if (source_is_archive(local)) {
            services_.extractor.extract(
                ExtractRequest{local, source_root}, events);
        } else {
            const auto destination = source_root / source.local_name;
            emit(events, EventKind::info,
                 "Copying source '" + local.string() + "'");
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(
                local, destination,
                std::filesystem::copy_options::overwrite_existing);
        }
    }

    assign_workspace(paths.work_dir, request.definition.execution.identity);
    auto staged = services_.recipes.run(
        RecipeRequest{definition, paths, source_root, package_root,
                      request.definition.execution}, events);

    if (staged.entries.empty())
        throw Error(ErrorCode::recipe_failed,
                    "build recipe produced an empty package root");

    const auto target =
        std::filesystem::absolute(paths.package_dir /
                                  package_filename(definition));
    if (std::filesystem::exists(target))
        std::filesystem::remove(target);

    if (!services_.packages.supports(definition.archive))
        throw Error(ErrorCode::invalid_configuration,
                    "package writer does not support requested archive");

    receipt.archive = services_.packages.write(
        PackageWriteRequest{std::move(staged), target, definition.archive}, events);
    receipt.package = receipt.archive.output;

    emit(events, EventKind::info,
         "Built package '" + receipt.package.string() + "'");
    return receipt;
}

} // namespace pkgbuild
