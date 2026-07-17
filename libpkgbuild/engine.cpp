#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/process.hpp>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace pkgbuild {
namespace {

class WorkGuard final {
public:
    WorkGuard(std::filesystem::path path, bool keep)
        : path_(std::move(path)), keep_(keep) {}

    ~WorkGuard()
    {
        if (!keep_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }

private:
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
    const auto definition = inspect(request.definition, events);
    const auto& paths = request.definition.paths;

    std::filesystem::create_directories(paths.source_dir);
    std::filesystem::create_directories(paths.package_dir);

    const auto source_root = std::filesystem::absolute(paths.work_dir / "src");
    const auto package_root = std::filesystem::absolute(paths.work_dir / "pkg");
    const auto temporary_root = std::filesystem::absolute(paths.work_dir / "tmp");

    std::filesystem::remove_all(paths.work_dir);
    std::filesystem::create_directories(source_root);
    std::filesystem::create_directories(package_root);
    std::filesystem::create_directories(temporary_root);
    WorkGuard work(paths.work_dir, request.keep_work);
    assign_workspace(paths.work_dir, request.definition.execution.identity);

    BuildReceipt receipt;
    receipt.definition = definition;

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
    services_.recipes.run(
        RecipeRequest{definition, paths, source_root, package_root,
                      request.definition.execution}, events);

    if (std::filesystem::is_empty(package_root))
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
        PackageWriteRequest{package_root, target, definition.archive}, events);
    receipt.package = receipt.archive.output;

    emit(events, EventKind::info,
         "Built package '" + receipt.package.string() + "'");
    return receipt;
}

} // namespace pkgbuild
