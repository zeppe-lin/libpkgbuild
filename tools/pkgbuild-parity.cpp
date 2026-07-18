#include "parity/archive_compare.hpp"

#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/process.hpp>
#include <pkgbuild/types.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <map>
#include <optional>
#include <pwd.h>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#ifndef PKGBUILD_PKGFILE_HELPER
#define PKGBUILD_PKGFILE_HELPER "/usr/libexec/pkgbuild-pkgfile"
#endif

#ifndef PKGBUILD_STAGE_SCANNER
#define PKGBUILD_STAGE_SCANNER "/usr/libexec/pkgbuild-stage-scan"
#endif

#ifndef PKGBUILD_FAKEROOT
#define PKGBUILD_FAKEROOT "/usr/bin/fakeroot"
#endif

#ifndef PKGBUILD_STRIP
#define PKGBUILD_STRIP "/usr/bin/strip"
#endif

namespace {

struct Options {
    std::filesystem::path pkgmk;
    std::filesystem::path pkgbuild;
    std::filesystem::path helper{PKGBUILD_PKGFILE_HELPER};
    std::filesystem::path scanner{PKGBUILD_STAGE_SCANNER};
    std::filesystem::path fakeroot{PKGBUILD_FAKEROOT};
    std::filesystem::path strip{PKGBUILD_STRIP};
    std::optional<std::string> build_user;
    std::optional<std::filesystem::path> config_file;
    std::optional<std::filesystem::path> manifest;
    std::filesystem::path work_base;
    bool download{false};
    bool keep_work{false};
    std::filesystem::path corpus;
};

[[noreturn]] void usage(const char* program, int status)
{
    std::ostream& output = status == 0 ? std::cout : std::cerr;
    output << "Usage: " << program
           << " [options] [CORPUS-DIRECTORY]\n"
           << "\n"
           << "Required options:\n"
           << "  --pkgmk FILE           legacy pkgmk executable\n"
           << "  --pkgbuild FILE        pkgbuild-example executable\n"
           << "\n"
           << "Other options:\n"
           << "  --helper FILE          pkgfile worker path\n"
           << "  --scanner FILE         staged metadata scanner path\n"
           << "  --fakeroot FILE        fakeroot frontend path\n"
           << "  --strip FILE           strip executable path\n"
           << "  --build-user USER      non-root identity for root callers\n"
           << "  --config FILE          source baseline pkgmk configuration\n"
           << "  --manifest FILE        read package directories from FILE\n"
           << "  --work-dir DIR         workspace base\n"
           << "  -d, --download         download missing URI sources\n"
           << "  --keep-work            retain generated corpus workspace\n"
           << "  -h, --help             show this help\n";
    std::exit(status);
}

std::string require_argument(int& index, int argc, char** argv)
{
    if (++index >= argc)
        usage(argv[0], 2);
    return argv[index];
}

std::filesystem::path absolute_program(const std::filesystem::path& path,
                                       const std::string& option)
{
    if (path.empty() || !path.is_absolute())
        throw std::runtime_error(option + " requires an absolute path");
    return path;
}

Options parse_options(int argc, char** argv)
{
    Options options;
    options.work_base = std::filesystem::current_path() / ".parity-work";

    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--pkgmk") {
            options.pkgmk = require_argument(i, argc, argv);
        } else if (option == "--pkgbuild") {
            options.pkgbuild = require_argument(i, argc, argv);
        } else if (option == "--helper") {
            options.helper = require_argument(i, argc, argv);
        } else if (option == "--scanner") {
            options.scanner = require_argument(i, argc, argv);
        } else if (option == "--fakeroot") {
            options.fakeroot = require_argument(i, argc, argv);
        } else if (option == "--strip") {
            options.strip = require_argument(i, argc, argv);
        } else if (option == "--build-user") {
            options.build_user = require_argument(i, argc, argv);
        } else if (option == "--config") {
            options.config_file = require_argument(i, argc, argv);
        } else if (option == "--manifest") {
            options.manifest = require_argument(i, argc, argv);
        } else if (option == "--work-dir") {
            options.work_base = require_argument(i, argc, argv);
        } else if (option == "-d" || option == "--download") {
            options.download = true;
        } else if (option == "--keep-work") {
            options.keep_work = true;
        } else if (option == "-h" || option == "--help") {
            usage(argv[0], 0);
        } else if (!option.empty() && option[0] == '-') {
            usage(argv[0], 2);
        } else if (!options.corpus.empty()) {
            usage(argv[0], 2);
        } else {
            options.corpus = option;
        }
    }

    if (options.pkgmk.empty() || options.pkgbuild.empty() ||
        (options.corpus.empty() == !options.manifest))
        usage(argv[0], 2);

    options.pkgmk = absolute_program(options.pkgmk, "--pkgmk");
    options.pkgbuild = absolute_program(options.pkgbuild, "--pkgbuild");
    options.helper = absolute_program(options.helper, "--helper");
    options.scanner = absolute_program(options.scanner, "--scanner");
    options.fakeroot = absolute_program(options.fakeroot, "--fakeroot");
    options.strip = absolute_program(options.strip, "--strip");
    if (options.config_file) {
        options.config_file = std::filesystem::absolute(*options.config_file);
        if (!std::filesystem::is_regular_file(*options.config_file))
            throw std::runtime_error(
                "baseline configuration is not a regular file: " +
                options.config_file->string());
    }
    if (options.manifest)
        options.manifest = std::filesystem::absolute(*options.manifest);
    options.work_base = std::filesystem::absolute(options.work_base);
    if (!options.corpus.empty())
        options.corpus = std::filesystem::absolute(options.corpus);
    return options;
}

pkgbuild::BuildIdentity build_identity(const std::string& name)
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    const int status = getpwnam_r(name.c_str(), &record, buffer.data(),
                                  buffer.size(), &result);
    if (status != 0 || result == nullptr)
        throw std::runtime_error("unknown build user: " + name);

    int group_count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &group_count);
    std::vector<gid_t> groups(static_cast<std::size_t>(group_count));
    if (group_count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(),
                     &group_count) < 0)
        throw std::runtime_error("cannot read groups for build user: " + name);
    groups.resize(static_cast<std::size_t>(group_count));

    return pkgbuild::BuildIdentity{
        record.pw_uid,
        record.pw_gid,
        std::move(groups),
        record.pw_dir,
        record.pw_name,
    };
}

std::map<std::string, std::string> selected_environment(
    const std::optional<pkgbuild::BuildIdentity>& identity)
{
    static const std::array<const char*, 10> names = {
        "PATH", "LANG", "LC_ALL", "MAKEFLAGS", "CFLAGS",
        "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "PKG_CONFIG_PATH", "TERM",
    };
    std::map<std::string, std::string> result;
    for (const char* name : names) {
        if (const char* value = std::getenv(name))
            result.emplace(name, value);
    }
    result["LANG"] = "C.UTF-8";
    result["LC_ALL"] = "C.UTF-8";
    if (identity) {
        result["HOME"] = identity->home.string();
        result["USER"] = identity->user;
        result["LOGNAME"] = identity->user;
    } else {
        if (const char* value = std::getenv("HOME"))
            result.emplace("HOME", value);
        if (const char* value = std::getenv("USER"))
            result.emplace("USER", value);
        if (const char* value = std::getenv("LOGNAME"))
            result.emplace("LOGNAME", value);
    }
    return result;
}

class Workspace final {
public:
    Workspace(const std::filesystem::path& base, bool keep)
        : path_(create(base)), keep_(keep) {}

    ~Workspace()
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

    void preserve() noexcept
    {
        keep_ = true;
    }

    void allow_identity_traversal(
        const std::optional<pkgbuild::BuildIdentity>& identity)
    {
        if (identity && geteuid() == 0 && chmod(path_.c_str(), 0711) != 0)
            throw std::runtime_error("cannot open parity workspace traversal: " +
                                     std::string(std::strerror(errno)));
    }

private:
    static std::filesystem::path create(const std::filesystem::path& base)
    {
        std::filesystem::create_directories(base);
        std::string pattern = (base / ".pkgbuild-parity.XXXXXX").string();
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        char* created = mkdtemp(storage.data());
        if (created == nullptr)
            throw std::runtime_error("cannot create parity workspace: " +
                                     std::string(std::strerror(errno)));
        return created;
    }

    std::filesystem::path path_;
    bool keep_;
};

void copy_recipe(const std::filesystem::path& source,
                 const std::filesystem::path& destination)
{
    std::filesystem::create_directories(destination);
    std::filesystem::copy(
        source, destination,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::copy_symlinks |
            std::filesystem::copy_options::overwrite_existing);
}

void change_owner(const std::filesystem::path& path,
                  const pkgbuild::BuildIdentity& identity)
{
    if (lchown(path.c_str(), identity.uid, identity.gid) != 0)
        throw std::runtime_error("cannot assign parity workspace ownership for '" +
                                 path.string() + "': " +
                                 std::strerror(errno));
}

void assign_tree(const std::filesystem::path& root,
                 const std::optional<pkgbuild::BuildIdentity>& identity)
{
    if (!identity || geteuid() != 0)
        return;
    change_owner(root, *identity);
    for (std::filesystem::recursive_directory_iterator iterator(root), end;
         iterator != end; ++iterator)
        change_owner(iterator->path(), *identity);
}

std::string shell_quote(const std::filesystem::path& path)
{
    std::string value = path.string();
    std::string result{"'"};
    for (const char character : value) {
        if (character == '\'')
            result += "'\\''";
        else
            result += character;
    }
    result += '\'';
    return result;
}

void write_build_config(
    const std::filesystem::path& filename,
    const std::optional<std::filesystem::path>& baseline,
    const std::filesystem::path& sources,
    const std::filesystem::path& packages,
    const std::filesystem::path& work)
{
    std::ofstream output(filename);
    if (!output)
        throw std::runtime_error("cannot write parity build configuration");
    if (baseline)
        output << ". " << shell_quote(*baseline) << '\n';
    output << "PKGMK_SOURCE_DIR=" << shell_quote(sources) << '\n'
           << "PKGMK_PACKAGE_DIR=" << shell_quote(packages) << '\n'
           << "PKGMK_WORK_DIR=" << shell_quote(work) << '\n'
           << "PKGMK_IGNORE_FOOTPRINT=yes\n";
    if (!baseline) {
        output << "PKGMK_ARCHIVE_FORMAT=gnutar\n"
               << "PKGMK_COMPRESSION_MODE=gz\n";
    }
    if (!output)
        throw std::runtime_error("cannot finish parity build configuration");
}

enum class CaseStatus {
    pass,
    legacy_build_failed,
    candidate_build_failed,
    nondeterministic_output,
    semantic_mismatch,
};

const char* status_name(CaseStatus status) noexcept
{
    switch (status) {
    case CaseStatus::pass: return "PASS";
    case CaseStatus::legacy_build_failed: return "LEGACY_BUILD_FAILED";
    case CaseStatus::candidate_build_failed: return "CANDIDATE_BUILD_FAILED";
    case CaseStatus::nondeterministic_output:
        return "NONDETERMINISTIC_OUTPUT";
    case CaseStatus::semantic_mismatch: return "SEMANTIC_MISMATCH";
    }
    return "UNKNOWN";
}

struct CaseResult {
    CaseStatus status{CaseStatus::pass};
    std::string name;
    std::filesystem::path source;
    std::filesystem::path root;
    std::vector<std::string> details;

    bool passed() const noexcept
    {
        return status == CaseStatus::pass;
    }
};

void write_text(const std::filesystem::path& path, const std::string& value)
{
    std::ofstream output(path);
    if (!output)
        throw std::runtime_error("cannot write parity evidence: " +
                                 path.string());
    output << value;
    if (!output)
        throw std::runtime_error("cannot finish parity evidence: " +
                                 path.string());
}

std::string process_status(const pkgbuild::ProcessResult& result)
{
    std::string diagnostic = "exit-status: " +
                             std::to_string(result.exit_status);
    if (result.termination_signal != 0)
        diagnostic += ", signal: " +
                      std::to_string(result.termination_signal);
    return diagnostic;
}

struct BuilderRun {
    std::optional<pkgbuild::ProcessResult> result;
    std::optional<std::string> error;
};

BuilderRun run_builder(const pkgbuild::ProcessExecutor& executor,
                       pkgbuild::ProcessRequest request,
                       const std::filesystem::path& log)
{
    request.capture_stdout = true;
    request.merge_stderr = true;
    try {
        auto result = executor.execute(request);
        write_text(log, result.stdout_data);
        if (!result.stdout_data.empty())
            std::cout << result.stdout_data;
        return BuilderRun{std::move(result), std::nullopt};
    } catch (const std::exception& error) {
        write_text(log, std::string("executor-error: ") + error.what() + "\n");
        return BuilderRun{std::nullopt, error.what()};
    }
}

CaseResult failed_case(CaseStatus status,
                       const std::string& name,
                       const std::filesystem::path& source,
                       const std::filesystem::path& root,
                       std::vector<std::string> details)
{
    return CaseResult{status, name, source, root, std::move(details)};
}

void validate_archive(const std::filesystem::path& archive)
{
    const auto comparison = pkgbuild::parity::compare_archives(archive, archive);
    if (!comparison.equivalent())
        throw std::runtime_error("archive is not self-equivalent");
}

void write_case_report(const CaseResult& result)
{
    std::ofstream output(result.root / "comparison.txt");
    if (!output)
        throw std::runtime_error("cannot write retained parity report");
    output << "case: " << result.name << '\n'
           << "status: " << status_name(result.status) << '\n'
           << "source: " << result.source.string() << '\n';
    for (const auto& detail : result.details)
        output << "detail: " << detail << '\n';
    if (!output)
        throw std::runtime_error("cannot finish retained parity report");
}

void retain_failure(const std::filesystem::path& workspace, CaseResult& result)
{
    const auto failed = workspace / "failed";
    std::filesystem::create_directories(failed);
    if (chmod(failed.c_str(), 0700) != 0)
        throw std::runtime_error("cannot protect retained parity directory: " +
                                 std::string(std::strerror(errno)));
    const auto destination = failed / result.name;
    if (std::filesystem::exists(destination))
        throw std::runtime_error("duplicate retained parity case: " +
                                 result.name);
    std::filesystem::rename(result.root, destination);
    result.root = destination;
    write_case_report(result);
}

void print_case_result(const CaseResult& result)
{
    std::cout << status_name(result.status) << ' ' << result.name << '\n';
    for (const auto& detail : result.details)
        std::cout << "  " << detail << '\n';
    if (!result.passed())
        std::cout << "  retained: " << result.root.string() << '\n';
}

std::filesystem::path find_package(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> packages;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file())
            continue;
        const auto name = item.path().filename().string();
        if (name.find(".pkg.tar.") != std::string::npos)
            packages.push_back(item.path());
    }
    if (packages.size() != 1)
        throw std::runtime_error("expected exactly one package in '" +
                                 directory.string() + "'");
    return std::filesystem::absolute(packages.front());
}

std::filesystem::path move_package(const std::filesystem::path& source,
                                   const std::filesystem::path& destination)
{
    const auto package = find_package(source);
    std::filesystem::create_directories(destination);
    const auto target = destination / package.filename();
    if (std::filesystem::exists(target))
        throw std::runtime_error("duplicate retained package: " +
                                 target.string());
    std::filesystem::rename(package, target);
    return std::filesystem::absolute(target);
}

std::filesystem::path find_private_workspace(
    const std::filesystem::path& base)
{
    std::vector<std::filesystem::path> workspaces;
    for (const auto& item : std::filesystem::directory_iterator(base)) {
        const auto filename = item.path().filename().string();
        if (item.is_directory() && filename.rfind(".pkgbuild.", 0) == 0)
            workspaces.push_back(std::filesystem::absolute(item.path()));
    }
    if (workspaces.size() != 1)
        throw std::runtime_error("expected exactly one retained private workspace in '" +
                                 base.string() + "'");
    return workspaces.front();
}

void reset_private_workspace(const std::filesystem::path& workspace)
{
    if (workspace.filename().string().rfind(".pkgbuild.", 0) != 0)
        throw std::runtime_error("refusing to reset non-private workspace: " +
                                 workspace.string());
    std::filesystem::remove_all(workspace);
}

void reset_temporary_directory(
    const std::filesystem::path& directory,
    const std::optional<pkgbuild::BuildIdentity>& identity)
{
    std::filesystem::remove_all(directory);
    std::filesystem::create_directory(directory);
    if (chmod(directory.c_str(), 0700) != 0)
        throw std::runtime_error("cannot protect parity temporary directory: " +
                                 std::string(std::strerror(errno)));
    assign_tree(directory, identity);
}

std::filesystem::path validate_case(const std::filesystem::path& value,
                                    const std::string& origin)
{
    std::error_code error;
    const auto path = std::filesystem::canonical(value, error);
    if (error)
        throw std::runtime_error(origin + ": cannot resolve package directory '" +
                                 value.string() + "': " + error.message());
    if (!std::filesystem::is_directory(path) ||
        !std::filesystem::is_regular_file(path / "Pkgfile"))
        throw std::runtime_error(origin + ": package directory has no Pkgfile: " +
                                 path.string());
    return path;
}

void validate_unique_case(
    const std::filesystem::path& path,
    const std::string& origin,
    std::set<std::filesystem::path>& paths,
    std::set<std::string>& names)
{
    if (!paths.insert(path).second)
        throw std::runtime_error(origin + ": duplicate package directory: " +
                                 path.string());
    const auto name = path.filename().string();
    if (name.empty() || !names.insert(name).second)
        throw std::runtime_error(origin + ": duplicate package basename: " + name);
}

std::string trim_manifest_line(const std::string& line)
{
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    const auto last = line.find_last_not_of(" \t\r");
    return line.substr(first, last - first + 1);
}

std::vector<std::filesystem::path> manifest_cases(
    const std::filesystem::path& manifest)
{
    std::ifstream input(manifest);
    if (!input)
        throw std::runtime_error("cannot open parity manifest: " +
                                 manifest.string());

    std::vector<std::filesystem::path> result;
    std::set<std::filesystem::path> paths;
    std::set<std::string> names;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto value = trim_manifest_line(line);
        if (value.empty() || value.front() == '#')
            continue;

        std::filesystem::path path(value);
        if (path.is_relative())
            path = manifest.parent_path() / path;
        const auto origin = manifest.string() + ":" +
                            std::to_string(line_number);
        path = validate_case(path, origin);
        validate_unique_case(path, origin, paths, names);
        result.push_back(std::move(path));
    }
    if (!input.eof())
        throw std::runtime_error("cannot read parity manifest: " +
                                 manifest.string());
    if (result.empty())
        throw std::runtime_error("parity manifest contains no package directories");
    return result;
}

std::vector<std::filesystem::path> corpus_cases(
    const std::filesystem::path& corpus)
{
    if (!std::filesystem::is_directory(corpus))
        throw std::runtime_error("corpus is not a directory: " + corpus.string());

    std::vector<std::filesystem::path> result;
    std::set<std::filesystem::path> paths;
    std::set<std::string> names;
    for (const auto& item : std::filesystem::directory_iterator(corpus)) {
        if (item.is_directory() &&
            std::filesystem::is_regular_file(item.path() / "Pkgfile")) {
            auto path = validate_case(item.path(), corpus.string());
            validate_unique_case(path, corpus.string(), paths, names);
            result.push_back(std::move(path));
        }
    }
    std::sort(result.begin(), result.end());
    if (result.empty())
        throw std::runtime_error("corpus contains no Pkgfile cases");
    return result;
}

struct BuildAttempt {
    std::optional<std::filesystem::path> package;
    std::optional<std::filesystem::path> workspace;
    std::vector<std::string> errors;

    bool ok() const noexcept
    {
        return package.has_value() && errors.empty();
    }
};

std::vector<std::string> compare_packages(
    const std::filesystem::path& reference,
    const std::filesystem::path& candidate)
{
    std::vector<std::string> details;
    if (reference.filename() != candidate.filename()) {
        details.push_back("package-filename: " +
                          reference.filename().string() + " -> " +
                          candidate.filename().string());
    }

    try {
        const auto comparison =
            pkgbuild::parity::compare_archives(reference, candidate);
        for (const auto& difference : comparison.differences)
            details.push_back(pkgbuild::parity::format_difference(difference));
    } catch (const std::exception& error) {
        details.push_back(std::string("comparison-error: ") + error.what());
    }
    return details;
}

std::vector<std::string> prefix_details(
    const std::string& prefix,
    const std::vector<std::string>& details)
{
    std::vector<std::string> result;
    result.reserve(details.size());
    for (const auto& detail : details)
        result.push_back(prefix + detail);
    return result;
}

void write_comparison_evidence(
    const std::filesystem::path& path,
    const std::vector<std::string>& details)
{
    std::string value;
    if (details.empty()) {
        value = "equivalent\n";
    } else {
        for (const auto& detail : details)
            value += detail + "\n";
    }
    write_text(path, value);
}

BuildAttempt run_candidate_attempt(
    const Options& options,
    const pkgbuild::ProcessExecutor& executor,
    const std::optional<pkgbuild::BuildIdentity>& identity,
    const std::map<std::string, std::string>& environment,
    const std::filesystem::path& recipe,
    const std::filesystem::path& sources,
    const std::filesystem::path& packages,
    const std::filesystem::path& work_base,
    const std::filesystem::path& temporary,
    const std::filesystem::path& config,
    const std::filesystem::path& evidence,
    std::optional<std::filesystem::path> exact_workspace = std::nullopt)
{
    std::filesystem::create_directories(evidence / "packages");
    std::vector<std::string> arguments = {
        "--source-dir", sources.string(),
        "--package-dir", packages.string(),
        "--work-dir", work_base.string(),
        "--tmp-dir", temporary.string(),
        "--config", config.string(),
        "--helper", options.helper.string(),
        "--scanner", options.scanner.string(),
        "--fakeroot", options.fakeroot.string(),
        "--strip", options.strip.string(),
        "--keep-work",
    };
    if (exact_workspace) {
        arguments.push_back("--workspace-dir");
        arguments.push_back(exact_workspace->string());
    }
    if (options.download)
        arguments.push_back("--download");
    arguments.push_back(recipe.string());

    const auto run = run_builder(
        executor,
        pkgbuild::ProcessRequest{
            options.pkgbuild,
            std::move(arguments),
            recipe,
            environment,
            identity,
            0022,
            true,
            true,
        },
        evidence / "build.log");
    if (run.error)
        return BuildAttempt{std::nullopt, std::nullopt,
                            {"executor-error: " + *run.error}};
    if (!run.result->ok())
        return BuildAttempt{std::nullopt, std::nullopt,
                            {process_status(*run.result)}};

    try {
        const auto package = move_package(packages, evidence / "packages");
        validate_archive(package);
        const auto workspace = find_private_workspace(work_base);
        write_text(evidence / "workspace.txt", workspace.string() + "\n");
        return BuildAttempt{package, workspace, {}};
    } catch (const std::exception& error) {
        return BuildAttempt{std::nullopt, std::nullopt,
                            {std::string("artifact-error: ") + error.what()}};
    }
}

BuildAttempt run_legacy_attempt(
    const Options& options,
    const pkgbuild::ProcessExecutor& executor,
    const std::optional<pkgbuild::BuildIdentity>& identity,
    const std::map<std::string, std::string>& environment,
    const std::filesystem::path& recipe,
    const std::filesystem::path& sources,
    const std::filesystem::path& packages,
    const std::filesystem::path& temporary,
    const std::filesystem::path& config,
    const std::filesystem::path& private_workspace,
    const std::filesystem::path& evidence)
{
    try {
        reset_private_workspace(private_workspace);
        reset_temporary_directory(temporary, identity);
        write_build_config(config, options.config_file, sources, packages,
                           private_workspace);
    } catch (const std::exception& error) {
        return BuildAttempt{std::nullopt, std::nullopt,
                            {std::string("workspace-error: ") + error.what()}};
    }

    std::filesystem::create_directories(evidence / "packages");
    auto legacy_environment = environment;
    legacy_environment["TMPDIR"] = temporary.string();
    std::vector<std::string> arguments = {
        "--", options.pkgmk.string(), "-cf", config.string(),
        "-f", "-if", "-kw",
    };
    if (options.download)
        arguments.push_back("-d");

    const auto run = run_builder(
        executor,
        pkgbuild::ProcessRequest{
            options.fakeroot,
            std::move(arguments),
            recipe,
            std::move(legacy_environment),
            identity,
            0022,
            true,
            true,
        },
        evidence / "build.log");
    if (run.error)
        return BuildAttempt{std::nullopt, std::nullopt,
                            {"executor-error: " + *run.error}};
    if (!run.result->ok())
        return BuildAttempt{std::nullopt, std::nullopt,
                            {process_status(*run.result)}};

    try {
        const auto package = move_package(packages, evidence / "packages");
        validate_archive(package);
        write_text(evidence / "workspace.txt",
                   private_workspace.string() + "\n");
        return BuildAttempt{package, private_workspace, {}};
    } catch (const std::exception& error) {
        return BuildAttempt{std::nullopt, std::nullopt,
                            {std::string("artifact-error: ") + error.what()}};
    }
}

CaseResult run_case(const Options& options,
                    const pkgbuild::ProcessExecutor& executor,
                    const std::optional<pkgbuild::BuildIdentity>& identity,
                    const std::map<std::string, std::string>& environment,
                    const std::filesystem::path& workspace,
                    const std::filesystem::path& corpus_case)
{
    const std::string name = corpus_case.filename().string();
    const auto root = workspace / name;
    const auto recipe = root / "recipe" / name;
    const auto legacy = root / "pkgmk";
    const auto candidate = root / "libpkgbuild";
    const auto packages = root / "packages";
    const auto sources = root / "sources";
    const auto work_base = root / "work";
    const auto temporary = root / "tmp";
    const auto config = root / "pkgmk.conf";

    copy_recipe(corpus_case, recipe);
    std::filesystem::create_directories(packages);
    std::filesystem::create_directories(sources);
    std::filesystem::create_directories(work_base);
    std::filesystem::create_directories(temporary);
    write_build_config(config, options.config_file, sources, packages, work_base);
    assign_tree(root, identity);

    const auto candidate_one = run_candidate_attempt(
        options, executor, identity, environment, recipe, sources, packages,
        work_base, temporary, config, candidate / "run-1");
    if (!candidate_one.ok()) {
        return failed_case(CaseStatus::candidate_build_failed, name, corpus_case,
                           root, candidate_one.errors);
    }

    const auto legacy_one = run_legacy_attempt(
        options, executor, identity, environment, recipe, sources, packages,
        temporary, config, *candidate_one.workspace, legacy / "run-1");
    if (!legacy_one.ok()) {
        return failed_case(CaseStatus::legacy_build_failed, name, corpus_case,
                           root, legacy_one.errors);
    }

    auto cross_details =
        compare_packages(*legacy_one.package, *candidate_one.package);
    if (!cross_details.empty())
        write_comparison_evidence(root / "cross-comparison.txt", cross_details);
    if (cross_details.empty())
        return CaseResult{CaseStatus::pass, name, corpus_case, root, {}};

    try {
        reset_private_workspace(*candidate_one.workspace);
        reset_temporary_directory(temporary, identity);
        write_build_config(config, options.config_file, sources, packages,
                           work_base);
    } catch (const std::exception& error) {
        return failed_case(
            CaseStatus::candidate_build_failed, name, corpus_case, root,
            {std::string("repeat-workspace-error: ") + error.what()});
    }

    const auto candidate_two = run_candidate_attempt(
        options, executor, identity, environment, recipe, sources, packages,
        work_base, temporary, config, candidate / "run-2",
        candidate_one.workspace);
    if (!candidate_two.ok()) {
        auto details = prefix_details("repeat-run: ", candidate_two.errors);
        return failed_case(CaseStatus::candidate_build_failed, name, corpus_case,
                           root, std::move(details));
    }

    const auto candidate_repeat =
        compare_packages(*candidate_one.package, *candidate_two.package);
    write_comparison_evidence(candidate / "repeat-comparison.txt",
                              candidate_repeat);
    if (!candidate_repeat.empty()) {
        std::vector<std::string> details{"engine: libpkgbuild"};
        auto repeated = prefix_details("repeat-mismatch: ", candidate_repeat);
        details.insert(details.end(), repeated.begin(), repeated.end());
        return failed_case(CaseStatus::nondeterministic_output, name,
                           corpus_case, root, std::move(details));
    }

    const auto legacy_two = run_legacy_attempt(
        options, executor, identity, environment, recipe, sources, packages,
        temporary, config, *candidate_two.workspace, legacy / "run-2");
    if (!legacy_two.ok()) {
        auto details = prefix_details("repeat-run: ", legacy_two.errors);
        return failed_case(CaseStatus::legacy_build_failed, name, corpus_case,
                           root, std::move(details));
    }

    const auto legacy_repeat =
        compare_packages(*legacy_one.package, *legacy_two.package);
    write_comparison_evidence(legacy / "repeat-comparison.txt",
                              legacy_repeat);
    if (!legacy_repeat.empty()) {
        std::vector<std::string> details{"engine: pkgmk"};
        auto repeated = prefix_details("repeat-mismatch: ", legacy_repeat);
        details.insert(details.end(), repeated.begin(), repeated.end());
        return failed_case(CaseStatus::nondeterministic_output, name,
                           corpus_case, root, std::move(details));
    }

    return failed_case(CaseStatus::semantic_mismatch, name, corpus_case,
                       root, std::move(cross_details));
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const auto options = parse_options(argc, argv);
        std::optional<pkgbuild::BuildIdentity> identity;
        if (options.build_user)
            identity = build_identity(*options.build_user);
        pkgbuild::validate_execution_policy(
            pkgbuild::ExecutionPolicy{identity, {}, 0022, std::nullopt});

        const auto environment = selected_environment(identity);
        Workspace workspace(options.work_base, options.keep_work);
        workspace.allow_identity_traversal(identity);
        pkgbuild::PosixProcessExecutor executor;

        std::map<CaseStatus, std::size_t> counts;
        const auto cases = options.manifest ? manifest_cases(*options.manifest)
                                            : corpus_cases(options.corpus);
        for (const auto& corpus_case : cases) {
            auto result = run_case(options, executor, identity, environment,
                                   workspace.path(), corpus_case);
            ++counts[result.status];
            if (result.passed()) {
                print_case_result(result);
                if (!options.keep_work)
                    std::filesystem::remove_all(result.root);
                continue;
            }

            workspace.preserve();
            retain_failure(workspace.path(), result);
            print_case_result(result);
        }

        std::cout << "SUMMARY"
                  << " pass=" << counts[CaseStatus::pass]
                  << " legacy-build-failed="
                  << counts[CaseStatus::legacy_build_failed]
                  << " candidate-build-failed="
                  << counts[CaseStatus::candidate_build_failed]
                  << " nondeterministic-output="
                  << counts[CaseStatus::nondeterministic_output]
                  << " semantic-mismatch="
                  << counts[CaseStatus::semantic_mismatch] << '\n';

        if (options.keep_work)
            std::cout << "WORK " << workspace.path().string() << '\n';
        const std::size_t failures =
            counts[CaseStatus::legacy_build_failed] +
            counts[CaseStatus::candidate_build_failed] +
            counts[CaseStatus::nondeterministic_output] +
            counts[CaseStatus::semantic_mismatch];
        if (failures != 0)
            std::cout << "FAILED_WORK "
                      << (workspace.path() / "failed").string() << '\n';
        return failures == 0 ? 0 : 1;
    } catch (const pkgbuild::Error& error) {
        std::cerr << "pkgbuild-parity: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "pkgbuild-parity: " << error.what() << '\n';
        return 2;
    }
}
