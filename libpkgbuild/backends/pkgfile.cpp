#include <pkgbuild/backends/fakeroot.hpp>
#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include "../process.hpp"
#include "../stage.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <cstdlib>
#include <fstream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace pkgbuild {
namespace {

std::string optional_path(const std::optional<std::filesystem::path>& path)
{
    return path ? std::filesystem::absolute(*path).string() : std::string{};
}

std::vector<std::string> split_sources(const std::string& value)
{
    std::istringstream input(value);
    std::vector<std::string> sources;
    std::string item;
    while (input >> item)
        sources.push_back(std::move(item));
    return sources;
}

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lowercase(std::string value)
{
    for (char& character : value)
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    return value;
}

Digest parse_md5(const std::string& value, std::size_t line)
{
    const std::string normalized = lowercase(value);
    if (normalized.size() != 32 ||
        !std::all_of(normalized.begin(), normalized.end(),
                     [](unsigned char character) {
                         return std::isxdigit(character) != 0;
                     }))
        throw Error(ErrorCode::invalid_definition,
                    "invalid MD5 digest on .md5sum line " +
                        std::to_string(line));
    return Digest{DigestAlgorithm::md5, normalized};
}

std::map<std::string, Digest>
read_md5_manifest(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw Error(ErrorCode::invalid_definition,
                    "checksum manifest not found: " + path.string());

    std::map<std::string, Digest> manifest;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#')
            continue;

        std::istringstream fields(line);
        std::string checksum;
        fields >> checksum;
        std::string filename;
        std::getline(fields, filename);
        filename = trim(std::move(filename));
        if (filename.empty())
            throw Error(ErrorCode::invalid_definition,
                        "missing filename on .md5sum line " +
                            std::to_string(line_number));
        if (filename.front() == '*')
            throw Error(ErrorCode::invalid_definition,
                        "binary-mode .md5sum entries are not supported");

        const std::filesystem::path name(filename);
        if (name.is_absolute() || name.filename() != name ||
            name == "." || name == "..")
            throw Error(ErrorCode::invalid_definition,
                        "invalid filename on .md5sum line " +
                            std::to_string(line_number) + ": " + filename);

        if (!manifest.emplace(filename, parse_md5(checksum, line_number)).second)
            throw Error(ErrorCode::invalid_definition,
                        "duplicate .md5sum entry for " + filename);
    }
    return manifest;
}

void attach_md5_manifest(std::vector<Source>& sources,
                         const std::filesystem::path& recipe_directory)
{
    const auto manifest_path = recipe_directory / ".md5sum";
    if (sources.empty()) {
        if (!std::filesystem::exists(manifest_path))
            return;
        const auto manifest = read_md5_manifest(manifest_path);
        if (!manifest.empty())
            throw Error(ErrorCode::invalid_definition,
                        ".md5sum contains entries but Pkgfile has no sources");
        return;
    }

    auto manifest = read_md5_manifest(manifest_path);
    std::map<std::string, bool> declared;
    for (auto& source : sources) {
        const std::string filename = source.local_name.filename().string();
        if (filename.empty())
            throw Error(ErrorCode::invalid_definition,
                        "source has no checksum filename: " +
                            source.declaration);
        if (!declared.emplace(filename, true).second)
            throw Error(ErrorCode::invalid_definition,
                        "multiple sources use checksum filename: " + filename);

        const auto item = manifest.find(filename);
        if (item == manifest.end())
            throw Error(ErrorCode::invalid_definition,
                        "missing .md5sum entry for " + filename);
        source.digests.push_back(item->second);
        manifest.erase(item);
    }

    if (!manifest.empty())
        throw Error(ErrorCode::invalid_definition,
                    "unknown .md5sum entry for " + manifest.begin()->first);
}

std::vector<std::string> common_arguments(const DefinitionRequest& request)
{
    return {
        std::filesystem::absolute(request.paths.recipe_dir).string(),
        optional_path(request.config_file),
        std::filesystem::absolute(request.paths.source_dir).string(),
        std::filesystem::absolute(request.paths.package_dir).string(),
        std::filesystem::absolute(request.paths.work_dir).string(),
        to_string(request.defaults.format),
        to_string(request.defaults.compression),
    };
}

std::pair<std::string, std::filesystem::path> current_user()
{
    std::array<char, 16384> buffer{};
    struct passwd record {};
    struct passwd* result = nullptr;
    if (getpwuid_r(geteuid(), &record, buffer.data(), buffer.size(), &result) == 0 &&
        result != nullptr) {
        return {
            result->pw_name ? result->pw_name : std::to_string(geteuid()),
            result->pw_dir ? result->pw_dir : "/",
        };
    }
    return {std::to_string(geteuid()), "/"};
}

std::map<std::string, std::string>
process_environment(const ExecutionPolicy& policy,
                    const std::filesystem::path& temporary_directory)
{
    static const std::array<const char*, 8> forbidden = {
        "LD_PRELOAD", "LD_LIBRARY_PATH", "BASH_ENV", "ENV",
        "CDPATH", "IFS", "PYTHONPATH", "PERL5LIB",
    };

    auto environment = policy.environment;
    for (const char* name : forbidden) {
        if (environment.find(name) != environment.end())
            throw Error(ErrorCode::invalid_configuration,
                        "unsafe recipe environment variable: " +
                            std::string(name));
    }

    environment.try_emplace("PATH", "/usr/bin:/bin");
    environment.try_emplace("LANG", "C");
    environment["TMPDIR"] = std::filesystem::absolute(temporary_directory).string();

    if (policy.identity) {
        environment["HOME"] = policy.identity->home.string();
        environment["USER"] = policy.identity->user;
        environment["LOGNAME"] = policy.identity->user;
    } else {
        const auto [user, home] = current_user();
        environment["HOME"] = home.string();
        environment["USER"] = user;
        environment["LOGNAME"] = user;
    }

    return environment;
}

class StateFile final {
public:
    StateFile(const std::filesystem::path& directory,
              const std::optional<BuildIdentity>& identity)
        : path_(create(directory, identity)) {}

    ~StateFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    static std::filesystem::path create(
        const std::filesystem::path& directory,
        const std::optional<BuildIdentity>& identity)
    {
        std::string pattern = (directory / "fakeroot.XXXXXX").string();
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        const int descriptor = mkstemp(storage.data());
        if (descriptor < 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot create fakeroot state file: " +
                            std::string(std::strerror(errno)));

        int saved = 0;
        if (fchmod(descriptor, 0600) != 0)
            saved = errno;
        if (saved == 0 && identity && geteuid() == 0 &&
            fchown(descriptor, identity->uid, identity->gid) != 0)
            saved = errno;
        if (close(descriptor) != 0 && saved == 0)
            saved = errno;
        if (saved != 0) {
            std::error_code ignored;
            std::filesystem::remove(storage.data(), ignored);
            throw Error(ErrorCode::filesystem_failed,
                        "cannot prepare fakeroot state file: " +
                            std::string(std::strerror(saved)));
        }
        return storage.data();
    }

    std::filesystem::path path_;
};

ProcessRequest process_request(const DefinitionRequest& request,
                               const std::filesystem::path& program,
                               std::vector<std::string> arguments,
                               bool capture_stdout,
                               const std::filesystem::path& temporary_directory)
{
    return ProcessRequest{
        std::filesystem::absolute(program),
        std::move(arguments),
        std::filesystem::absolute(request.paths.recipe_dir),
        process_environment(request.execution, temporary_directory),
        request.execution.identity,
        request.execution.file_creation_mask,
        capture_stdout,
        true,
    };
}

} // namespace

PackageDefinition PkgfileDefinitionLoader::load(const DefinitionRequest& request,
                                                EventSink& events) const
{
    emit(events, EventKind::info,
         "Loading package definition with " + std::string(name()));

    auto arguments = common_arguments(request);
    arguments.insert(arguments.begin(), "inspect");

    const auto process = processes_.execute(process_request(
        request, helper_, std::move(arguments), true, "/tmp"));
    if (!process.ok())
        throw Error(ErrorCode::invalid_definition,
                    "Pkgfile worker failed with status " +
                        std::to_string(process.exit_status));

    const auto fields = detail::split_nul(process.stdout_data);
    if (fields.size() != 8 || fields[0] != "pkgfile/0")
        throw Error(ErrorCode::invalid_definition,
                    "Pkgfile worker returned an unsupported definition record");

    PackageDefinition definition;
    definition.id = PackageId{fields[1], fields[2], fields[3]};
    for (const auto& source : split_sources(fields[4]))
        definition.sources.push_back(parse_source(source));
    attach_md5_manifest(definition.sources,
                        std::filesystem::absolute(request.paths.recipe_dir));
    definition.archive = ArchiveSpec{
        archive_format_from_string(fields[5]),
        compression_from_string(fields[6]),
    };
    definition.recipe = Recipe{
        RecipeFormat::pkgfile_v0,
        std::filesystem::absolute(request.paths.recipe_dir / "Pkgfile"),
        request.config_file ?
            std::optional<std::filesystem::path>(
                std::filesystem::absolute(*request.config_file)) :
            std::nullopt,
        fields[7],
    };

    return definition;
}

StagedPackage PosixShellRecipeRunner::run(const RecipeRequest& request,
                                          EventSink& events) const
{
    if (request.definition.recipe.format != RecipeFormat::pkgfile_v0)
        throw Error(ErrorCode::recipe_failed,
                    "POSIX shell runner cannot execute this recipe format");

    emit(events, EventKind::info,
         "Running recipe with " + std::string(name()));

    std::vector<std::string> arguments = {
        "run",
        std::filesystem::absolute(request.paths.recipe_dir).string(),
        optional_path(request.definition.recipe.config_file),
        std::filesystem::absolute(request.paths.source_dir).string(),
        std::filesystem::absolute(request.paths.package_dir).string(),
        std::filesystem::absolute(request.paths.work_dir).string(),
        to_string(request.definition.archive.format),
        to_string(request.definition.archive.compression),
        std::filesystem::absolute(request.source_root).string(),
        std::filesystem::absolute(request.package_root).string(),
        request.definition.id.name,
        request.definition.id.version,
        request.definition.id.release,
    };

    DefinitionRequest execution_request{
        request.paths,
        request.definition.recipe.config_file,
        request.definition.archive,
        request.execution,
    };
    const auto process = processes_.execute(process_request(
        execution_request, helper_, std::move(arguments), false,
        request.paths.work_dir / "tmp"));
    if (!process.ok())
        throw Error(ErrorCode::recipe_failed,
                    "build recipe failed with status " +
                        std::to_string(process.exit_status));

    return scan_staged_package(request.package_root);
}


StagedPackage FakerootPkgfileRecipeRunner::run(
    const RecipeRequest& request, EventSink& events) const
{
    if (request.definition.recipe.format != RecipeFormat::pkgfile_v0)
        throw Error(ErrorCode::recipe_failed,
                    "fakeroot Pkgfile runner cannot execute this recipe format");

    emit(events, EventKind::info,
         "Running recipe with " + std::string(name()));

    const auto metadata = request.paths.work_dir / "metadata";
    StateFile state(metadata, request.execution.identity);

    std::vector<std::string> worker_arguments = {
        "run",
        std::filesystem::absolute(request.paths.recipe_dir).string(),
        optional_path(request.definition.recipe.config_file),
        std::filesystem::absolute(request.paths.source_dir).string(),
        std::filesystem::absolute(request.paths.package_dir).string(),
        std::filesystem::absolute(request.paths.work_dir).string(),
        to_string(request.definition.archive.format),
        to_string(request.definition.archive.compression),
        std::filesystem::absolute(request.source_root).string(),
        std::filesystem::absolute(request.package_root).string(),
        request.definition.id.name,
        request.definition.id.version,
        request.definition.id.release,
    };

    std::vector<std::string> fakeroot_arguments = {
        "-s",
        std::filesystem::absolute(state.path()).string(),
        "--",
        std::filesystem::absolute(helper_).string(),
    };
    fakeroot_arguments.insert(fakeroot_arguments.end(),
                              worker_arguments.begin(),
                              worker_arguments.end());

    DefinitionRequest execution_request{
        request.paths,
        request.definition.recipe.config_file,
        request.definition.archive,
        request.execution,
    };
    const auto recipe = processes_.execute(process_request(
        execution_request, fakeroot_, std::move(fakeroot_arguments), false,
        request.paths.work_dir / "tmp"));
    if (!recipe.ok())
        throw Error(ErrorCode::recipe_failed,
                    "fakeroot build recipe failed with status " +
                        std::to_string(recipe.exit_status));

    std::vector<std::string> scanner_arguments = {
        "-i",
        std::filesystem::absolute(state.path()).string(),
        "--",
        std::filesystem::absolute(scanner_).string(),
        std::filesystem::absolute(request.package_root).string(),
    };
    const auto scan = processes_.execute(process_request(
        execution_request, fakeroot_, std::move(scanner_arguments), true,
        request.paths.work_dir / "tmp"));
    if (!scan.ok())
        throw Error(ErrorCode::recipe_failed,
                    "fakeroot staged metadata scan failed with status " +
                        std::to_string(scan.exit_status));

    return detail::parse_staged_manifest(request.package_root,
                                         scan.stdout_data);
}

} // namespace pkgbuild
