#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/footprint.hpp>
#include <pkgbuild/process.hpp>
#include <pkgbuild/stage.hpp>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
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

bool path_within(const std::filesystem::path& root,
                 const std::filesystem::path& candidate)
{
    auto expected = root.begin();
    auto observed = candidate.begin();
    for (; expected != root.end(); ++expected, ++observed) {
        if (observed == candidate.end() || *observed != *expected)
            return false;
    }
    return true;
}

void reject_snapshot_target(const std::filesystem::path& snapshot_root,
                            const std::filesystem::path& candidate,
                            const char* description)
{
    if (path_within(snapshot_root, normalized(candidate)))
        throw Error(ErrorCode::invalid_configuration,
                    std::string(description) +
                        " must not be inside the source snapshot");
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
    PrivateWorkspace(
        const BuildPaths& paths,
        bool keep,
        const std::optional<std::filesystem::path>& requested)
        : path_(create(prepare_work_base(paths), requested)), keep_(keep) {}

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
    static std::filesystem::path create_random(
        const std::filesystem::path& base)
    {
        std::string pattern = (base / ".pkgbuild.XXXXXX").string();
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');

        char* created = mkdtemp(storage.data());
        if (created == nullptr)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot create private build workspace in " +
                            base.string() + ": " + std::strerror(errno));
        return created;
    }

    static std::filesystem::path create_exact(
        const std::filesystem::path& base,
        const std::filesystem::path& requested)
    {
        if (requested.empty() || !requested.is_absolute())
            throw Error(ErrorCode::invalid_configuration,
                        "exact workspace path must be absolute");

        const auto result = requested.lexically_normal();
        const auto filename = result.filename().string();
        if (filename.size() <= std::string(".pkgbuild.").size() ||
            filename.rfind(".pkgbuild.", 0) != 0)
            throw Error(ErrorCode::invalid_configuration,
                        "exact workspace must be a .pkgbuild.* child");
        if (normalized(result.parent_path()) != base)
            throw Error(ErrorCode::invalid_configuration,
                        "exact workspace must be a direct child of the work base");

        std::error_code status_error;
        const auto status = std::filesystem::symlink_status(result, status_error);
        if (!status_error && status.type() != std::filesystem::file_type::not_found)
            throw Error(ErrorCode::invalid_configuration,
                        "exact workspace path already exists: " + result.string());
        if (status_error &&
            status_error != std::errc::no_such_file_or_directory)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot inspect exact workspace path: " +
                            status_error.message());

        if (mkdir(result.c_str(), 0700) != 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot create exact private build workspace: " +
                            std::string(std::strerror(errno)));
        return result;
    }

    static std::filesystem::path create(
        const std::filesystem::path& base,
        const std::optional<std::filesystem::path>& requested)
    {
        const auto result = requested ? create_exact(base, *requested)
                                      : create_random(base);
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

void materialize_snapshot(const pkgsource::source_snapshot& snapshot,
                          const std::filesystem::path& destination)
{
    if (std::filesystem::exists(destination))
        throw Error(ErrorCode::filesystem_failed,
                    "snapshot materialization already exists: " +
                        destination.string());

    std::filesystem::create_directory(destination);
    const auto source = snapshot.native_root();
    for (const auto& entry : std::filesystem::directory_iterator(source)) {
        std::filesystem::copy(
            entry.path(), destination / entry.path().filename(),
            std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks);
    }
    if (chmod(destination.c_str(), 0500) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot seal materialized recipe root: " +
                        std::string(std::strerror(errno)));
}

std::filesystem::path source_path(const BuildSource& source,
                                  const BuildPaths& paths)
{
    if (source.captured)
        return source.captured->native_path();
    return std::filesystem::absolute(paths.source_dir /
                                     source.input.local_name);
}

std::filesystem::path local_source_path(const Source& source,
                                        const BuildPaths& paths)
{
    if (source.uri)
        return std::filesystem::absolute(paths.source_dir / source.local_name);
    return std::filesystem::absolute(paths.recipe_dir / source.local_name);
}

class FileDescriptor final
{
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor()
    {
        if (value_ >= 0)
            (void)close(value_);
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const noexcept { return value_; }

private:
    int value_;
};

[[noreturn]] void source_copy_error(const std::string& operation,
                                    const std::filesystem::path& path)
{
    throw Error(ErrorCode::filesystem_failed,
                operation + " '" + path.string() + "': " +
                    std::strerror(errno));
}

void copy_verified_source(const VerifiedSource& source,
                          const std::filesystem::path& destination)
{
    FileDescriptor input(source.duplicate_descriptor());
    if (lseek(input.get(), 0, SEEK_SET) < 0)
        source_copy_error("cannot rewind verified source", source.path());

    struct stat status {};
    if (fstat(input.get(), &status) != 0)
        source_copy_error("cannot inspect verified source", source.path());
    if (!S_ISREG(status.st_mode))
        throw Error(ErrorCode::filesystem_failed,
                    "verified source is not a regular file: " +
                        source.path().string());

    FileDescriptor output(open(destination.c_str(),
                               O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC |
                                   O_NOFOLLOW,
                               status.st_mode & 0777));
    if (output.get() < 0)
        source_copy_error("cannot create source copy", destination);
    if (fchmod(output.get(), status.st_mode & 0777) != 0)
        source_copy_error("cannot set source copy mode", destination);

    char buffer[64 * 1024];
    for (;;) {
        const ssize_t count = read(input.get(), buffer, sizeof(buffer));
        if (count > 0) {
            std::size_t offset = 0;
            while (offset != static_cast<std::size_t>(count)) {
                const ssize_t written = write(
                    output.get(), buffer + offset,
                    static_cast<std::size_t>(count) - offset);
                if (written > 0) {
                    offset += static_cast<std::size_t>(written);
                    continue;
                }
                if (written < 0 && errno == EINTR)
                    continue;
                source_copy_error("cannot write source copy", destination);
            }
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        source_copy_error("cannot read verified source", source.path());
    }
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

std::filesystem::path execution_temporary_directory(
    const ExecutionPolicy& execution,
    const BuildPaths& paths)
{
    if (execution.temporary_directory)
        return normalized(*execution.temporary_directory);
    return std::filesystem::absolute(paths.work_dir / "tmp").lexically_normal();
}

void prepare_temporary_directory(
    const std::filesystem::path& directory,
    const std::optional<BuildIdentity>& identity)
{
    if (directory.empty() || is_root_path(directory))
        throw Error(ErrorCode::invalid_configuration,
                    "temporary directory must not be the filesystem root");

    const auto before = std::filesystem::symlink_status(directory);
    if (std::filesystem::is_symlink(before))
        throw Error(ErrorCode::invalid_configuration,
                    "temporary directory must not be a symbolic link");

    std::filesystem::create_directories(directory);
    const auto after = std::filesystem::symlink_status(directory);
    if (!std::filesystem::is_directory(after) ||
        std::filesystem::is_symlink(after))
        throw Error(ErrorCode::invalid_configuration,
                    "temporary directory is not a real directory");

    if (chmod(directory.c_str(), 0700) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot protect temporary directory " +
                        directory.string() + ": " + std::strerror(errno));
    assign_workspace(directory, identity);
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

void seal_workspace(const std::filesystem::path& root,
                    const std::optional<BuildIdentity>& identity)
{
    if (!identity || geteuid() != 0)
        return;
    if (lchown(root.c_str(), 0, 0) != 0 || chmod(root.c_str(), 0700) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot seal build workspace " + root.string() + ": " +
                        std::strerror(errno));
}

} // namespace

PackageDefinition Engine::inspect(const DefinitionRequest& request,
                                  EventSink& events) const
{
    if (legacy_definitions_ == nullptr)
        throw Error(ErrorCode::invalid_configuration,
                    "mutable package-source inspection is not configured");
    validate_execution_policy(request.execution);
    if (request.execution.temporary_directory)
        prepare_temporary_directory(
            execution_temporary_directory(request.execution, request.paths),
            request.execution.identity);
    return legacy_definitions_->load(request, events);
}

LegacyBuildReceipt Engine::build(const BuildRequest& request,
                                 EventSink& events) const
{
    validate_execution_policy(request.definition.execution);

    const auto& configured_paths = request.definition.paths;
    std::filesystem::create_directories(configured_paths.source_dir);
    std::filesystem::create_directories(configured_paths.package_dir);

    PrivateWorkspace workspace(configured_paths, request.keep_work,
                               request.workspace_directory);
    BuildPaths paths = configured_paths;
    paths.work_dir = workspace.path();

    std::filesystem::create_directories(paths.work_dir / "src");
    std::filesystem::create_directories(paths.work_dir / "pkg");
    prepare_temporary_directory(
        execution_temporary_directory(request.definition.execution, paths),
        request.definition.execution.identity);
    assign_workspace(paths.work_dir, request.definition.execution.identity);
    prepare_metadata_directory(paths.work_dir,
                               request.definition.execution.identity);

    DefinitionRequest definition_request = request.definition;
    definition_request.paths = paths;
    const auto definition = inspect(definition_request, events);

    const auto source_root = std::filesystem::absolute(paths.work_dir / "src");
    const auto package_root = std::filesystem::absolute(paths.work_dir / "pkg");

    LegacyBuildReceipt receipt;
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

        if (source.digests.empty())
            throw Error(ErrorCode::invalid_definition,
                        "source has no declared checksum: " +
                            source.declaration);

        auto verified =
            services_.verifier.verify(local, source.digests, events);
        receipt.verifications.insert(receipt.verifications.end(),
                                     verified.receipts().begin(),
                                     verified.receipts().end());

        if (source_is_archive(local)) {
            services_.extractor.extract(
                ExtractRequest{verified, source_root}, events);
        } else {
            const auto destination =
                source_root / source.local_name.filename();
            emit(events, EventKind::info,
                 "Copying verified source '" + local.string() + "'");
            copy_verified_source(verified, destination);
        }
        services_.verifier.revalidate(verified, events);
    }

    assign_workspace(paths.work_dir, request.definition.execution.identity);
    auto staged = services_.recipes.run(
        RecipeRequest{definition, paths, source_root, package_root,
                      request.definition.execution}, events);

    if (staged.entries.empty())
        throw Error(ErrorCode::recipe_failed,
                    "build recipe produced an empty package root");

    auto transformation = services_.transformer.transform(
        PackageTransformRequest{staged, definition, request.transformations,
                                request.definition.execution},
        events);
    if (!transformation.changes.empty())
        receipt.transformations.push_back(std::move(transformation));
    validate_staged_package(staged);

    if (request.footprint.action != FootprintAction::ignore) {
        const auto manifest = std::filesystem::absolute(
            request.footprint.manifest.value_or(
                configured_paths.recipe_dir / ".footprint"));
        FootprintReceipt footprint;
        footprint.manifest = manifest;
        footprint.actual = footprint_from_staged_package(staged);

        if (request.footprint.action == FootprintAction::compare) {
            emit(events, EventKind::info,
                 "Checking footprint '" + manifest.string() + "'");
            footprint.expected = read_footprint(manifest);
            footprint.difference = compare_footprints(
                *footprint.expected, footprint.actual);
            if (!footprint.difference.empty())
                throw FootprintMismatch(manifest, footprint.difference);
        } else if (request.footprint.action == FootprintAction::write) {
            emit(events, EventKind::info,
                 "Writing footprint '" + manifest.string() + "'");
            write_footprint(manifest, footprint.actual);
            footprint.written = true;
        }
        receipt.footprint = std::move(footprint);
    }

    seal_workspace(paths.work_dir, request.definition.execution.identity);

    const auto target =
        std::filesystem::absolute(paths.package_dir /
                                  package_filename(definition));
    if (std::filesystem::exists(target))
        std::filesystem::remove(target);

    if (!services_.packages.supports(definition.archive))
        throw Error(ErrorCode::invalid_configuration,
                    "package writer does not support requested archive");

    receipt.archive = services_.packages.write(
        PackageWriteRequest{std::move(staged), target, definition.archive},
        events);
    receipt.package = receipt.archive.output;

    emit(events, EventKind::info,
         "Built package '" + receipt.package.string() + "'");
    return receipt;
}

BuildReceipt Engine::build(const BuildDefinition& definition,
                           const BuildEnvironment& environment,
                           EventSink& events) const
{
    validate_execution_policy(environment.execution);
    if (environment.source_directory.empty() ||
        environment.package_directory.empty() ||
        environment.work_directory.empty())
        throw Error(ErrorCode::invalid_configuration,
                    "build environment directories must be explicit");

    BuildPaths configured_paths{
        definition.snapshot().native_root(),
        std::filesystem::absolute(environment.source_directory),
        std::filesystem::absolute(environment.package_directory),
        std::filesystem::absolute(environment.work_directory),
    };
    const auto snapshot_root = normalized(configured_paths.recipe_dir);
    reject_snapshot_target(snapshot_root, configured_paths.source_dir,
                           "source cache");
    reject_snapshot_target(snapshot_root, configured_paths.package_dir,
                           "package output directory");
    reject_snapshot_target(snapshot_root, configured_paths.work_dir,
                           "work directory");
    std::filesystem::create_directories(configured_paths.source_dir);
    std::filesystem::create_directories(configured_paths.package_dir);

    PrivateWorkspace workspace(configured_paths, environment.keep_work,
                               environment.workspace_directory);
    BuildPaths paths = configured_paths;
    paths.work_dir = workspace.path();
    paths.recipe_dir = paths.work_dir / "recipe";

    materialize_snapshot(definition.snapshot(), paths.recipe_dir);
    std::filesystem::create_directories(paths.work_dir / "src");
    std::filesystem::create_directories(paths.work_dir / "pkg");
    prepare_temporary_directory(
        execution_temporary_directory(environment.execution, paths),
        environment.execution.identity);
    assign_workspace(paths.work_dir, environment.execution.identity);
    prepare_metadata_directory(paths.work_dir,
                               environment.execution.identity);

    const auto source_root = std::filesystem::absolute(paths.work_dir / "src");
    const auto package_root = std::filesystem::absolute(paths.work_dir / "pkg");

    BuildReceipt receipt{
        definition,
        {},
        {},
        {},
        {},
        std::nullopt,
        {},
        std::nullopt,
    };
    if (environment.keep_work)
        receipt.work_directory = workspace.path();

    for (const auto& declared : definition.sources()) {
        const auto& source = declared.input;
        auto local = source_path(declared, paths);
        if (!declared.captured && !std::filesystem::exists(local)) {
            if (!source.uri)
                throw Error(ErrorCode::invalid_definition,
                            "uncaptured source has no remote locator: " +
                                source.declaration);
            if (!environment.download_missing)
                throw Error(ErrorCode::missing_source,
                            "source not found; enable downloading: " +
                                local.string());
            receipt.downloads.push_back(
                services_.downloader.fetch(
                    DownloadRequest{*source.uri, local}, events));
        }

        if (source.digests.empty())
            throw Error(ErrorCode::invalid_definition,
                        "source has no declared checksum: " +
                            source.declaration);

        auto verified =
            services_.verifier.verify(local, source.digests, events);
        receipt.verifications.insert(receipt.verifications.end(),
                                     verified.receipts().begin(),
                                     verified.receipts().end());

        if (source_is_archive(source.local_name)) {
            services_.extractor.extract(
                ExtractRequest{verified, source_root}, events);
        } else {
            const auto destination = source_root / source.local_name.filename();
            emit(events, EventKind::info,
                 "Copying verified source '" + local.string() + "'");
            copy_verified_source(verified, destination);
        }
        services_.verifier.revalidate(verified, events);
    }

    assign_workspace(paths.work_dir, environment.execution.identity);
    auto staged = services_.recipes.run_captured(
        CapturedRecipeRequest{definition, paths, source_root, package_root,
                              environment.execution},
        events);
    if (staged.entries.empty())
        throw Error(ErrorCode::recipe_failed,
                    "build recipe produced an empty package root");

    auto transformation = services_.transformer.transform_definition(
        DefinitionTransformRequest{staged, definition,
                                   environment.execution},
        events);
    if (!transformation.changes.empty())
        receipt.transformations.push_back(std::move(transformation));
    validate_staged_package(staged);

    const auto& footprint_policy = definition.policy().footprint;
    if (footprint_policy.action != FootprintAction::ignore) {
        FootprintReceipt footprint;
        footprint.actual = footprint_from_staged_package(staged);

        if (footprint_policy.action == FootprintAction::compare) {
            if (!definition.footprint())
                throw Error(ErrorCode::invalid_definition,
                            "captured footprint disappeared from definition");
            footprint.manifest =
                definition.footprint()->file().native_path();
            emit(events, EventKind::info,
                 "Checking captured footprint '" +
                     footprint.manifest.string() + "'");
            footprint.expected = read_footprint(footprint.manifest);
            footprint.difference = compare_footprints(
                *footprint.expected, footprint.actual);
            if (!footprint.difference.empty())
                throw FootprintMismatch(footprint.manifest,
                                        footprint.difference);
        } else if (footprint_policy.action == FootprintAction::write) {
            footprint.manifest = *footprint_policy.manifest;
            emit(events, EventKind::info,
                 "Writing footprint '" + footprint.manifest.string() + "'");
            write_footprint(footprint.manifest, footprint.actual);
            footprint.written = true;
        }
        receipt.footprint = std::move(footprint);
    }

    seal_workspace(paths.work_dir, environment.execution.identity);

    const auto target = std::filesystem::absolute(
        paths.package_dir / package_filename(definition));
    if (std::filesystem::exists(target))
        std::filesystem::remove(target);

    const auto& archive = definition.policy().archive;
    if (!services_.packages.supports(archive))
        throw Error(ErrorCode::invalid_configuration,
                    "package writer does not support requested archive");
    receipt.archive = services_.packages.write(
        PackageWriteRequest{std::move(staged), target, archive}, events);
    receipt.package = receipt.archive.output;

    emit(events, EventKind::info,
         "Built package '" + receipt.package.string() + "' from snapshot " +
             definition.source_snapshot_fingerprint().hex());
    return receipt;
}

} // namespace pkgbuild
