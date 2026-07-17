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
    std::filesystem::path work_base;
    bool keep_work{false};
    std::filesystem::path corpus;
};

[[noreturn]] void usage(const char* program, int status)
{
    std::ostream& output = status == 0 ? std::cout : std::cerr;
    output << "Usage: " << program << " [options] CORPUS-DIRECTORY\n"
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
           << "  --work-dir DIR         workspace base\n"
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
        } else if (option == "--work-dir") {
            options.work_base = require_argument(i, argc, argv);
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
        options.corpus.empty())
        usage(argv[0], 2);

    options.pkgmk = absolute_program(options.pkgmk, "--pkgmk");
    options.pkgbuild = absolute_program(options.pkgbuild, "--pkgbuild");
    options.helper = absolute_program(options.helper, "--helper");
    options.scanner = absolute_program(options.scanner, "--scanner");
    options.fakeroot = absolute_program(options.fakeroot, "--fakeroot");
    options.strip = absolute_program(options.strip, "--strip");
    options.work_base = std::filesystem::absolute(options.work_base);
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

void write_pkgmk_config(const std::filesystem::path& filename,
                        const std::filesystem::path& sources,
                        const std::filesystem::path& packages,
                        const std::filesystem::path& work)
{
    std::ofstream output(filename);
    if (!output)
        throw std::runtime_error("cannot write pkgmk parity configuration");
    output << "PKGMK_SOURCE_DIR=" << shell_quote(sources) << '\n'
           << "PKGMK_PACKAGE_DIR=" << shell_quote(packages) << '\n'
           << "PKGMK_WORK_DIR=" << shell_quote(work) << '\n'
           << "PKGMK_IGNORE_FOOTPRINT=yes\n"
           << "PKGMK_ARCHIVE_FORMAT=gnutar\n"
           << "PKGMK_COMPRESSION_MODE=gz\n";
    if (!output)
        throw std::runtime_error("cannot finish pkgmk parity configuration");
}

void run_checked(const pkgbuild::ProcessExecutor& executor,
                 const pkgbuild::ProcessRequest& request,
                 const std::string& label)
{
    const auto result = executor.execute(request);
    if (result.ok())
        return;
    std::string diagnostic = label + " failed with status " +
                             std::to_string(result.exit_status);
    if (result.termination_signal != 0)
        diagnostic += " (signal " +
                      std::to_string(result.termination_signal) + ")";
    if (!result.stdout_data.empty())
        diagnostic += "\n" + result.stdout_data;
    throw std::runtime_error(diagnostic);
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

std::vector<std::filesystem::path> corpus_cases(
    const std::filesystem::path& corpus)
{
    if (!std::filesystem::is_directory(corpus))
        throw std::runtime_error("corpus is not a directory: " + corpus.string());

    std::vector<std::filesystem::path> result;
    for (const auto& item : std::filesystem::directory_iterator(corpus)) {
        if (item.is_directory() &&
            std::filesystem::is_regular_file(item.path() / "Pkgfile"))
            result.push_back(item.path());
    }
    std::sort(result.begin(), result.end());
    if (result.empty())
        throw std::runtime_error("corpus contains no Pkgfile cases");
    return result;
}

bool run_case(const Options& options,
              const pkgbuild::ProcessExecutor& executor,
              const std::optional<pkgbuild::BuildIdentity>& identity,
              const std::map<std::string, std::string>& environment,
              const std::filesystem::path& workspace,
              const std::filesystem::path& corpus_case)
{
    const std::string name = corpus_case.filename().string();
    const auto root = workspace / name;
    const auto legacy = root / "pkgmk";
    const auto candidate = root / "libpkgbuild";
    const auto legacy_recipe = legacy / "recipe";
    const auto candidate_recipe = candidate / "recipe";
    const auto legacy_packages = legacy / "packages";
    const auto candidate_packages = candidate / "packages";
    const auto legacy_sources = legacy / "sources";
    const auto candidate_sources = candidate / "sources";
    const auto legacy_work = legacy / "work";
    const auto candidate_work = candidate / "work";

    copy_recipe(corpus_case, legacy_recipe);
    copy_recipe(corpus_case, candidate_recipe);
    std::filesystem::create_directories(legacy_packages);
    std::filesystem::create_directories(candidate_packages);
    std::filesystem::create_directories(legacy_sources);
    std::filesystem::create_directories(candidate_sources);
    std::filesystem::create_directories(legacy_work);
    std::filesystem::create_directories(candidate_work);

    const auto config = legacy / "pkgmk.conf";
    write_pkgmk_config(config, legacy_sources, legacy_packages, legacy_work);
    assign_tree(root, identity);

    run_checked(
        executor,
        pkgbuild::ProcessRequest{
            options.fakeroot,
            {"--", options.pkgmk.string(), "-cf", config.string(),
             "-f", "-if"},
            legacy_recipe,
            environment,
            identity,
            0022,
            false,
            true,
        },
        "pkgmk case '" + name + "'");

    run_checked(
        executor,
        pkgbuild::ProcessRequest{
            options.pkgbuild,
            {"--source-dir", candidate_sources.string(),
             "--package-dir", candidate_packages.string(),
             "--work-dir", candidate_work.string(),
             "--helper", options.helper.string(),
             "--scanner", options.scanner.string(),
             "--fakeroot", options.fakeroot.string(),
             "--strip", options.strip.string(),
             candidate_recipe.string()},
            candidate_recipe,
            environment,
            identity,
            0022,
            true,
            true,
        },
        "libpkgbuild case '" + name + "'");

    const auto reference = find_package(legacy_packages);
    const auto result = find_package(candidate_packages);
    const bool same_filename =
        reference.filename().string() == result.filename().string();
    const auto comparison = pkgbuild::parity::compare_archives(reference, result);
    if (same_filename && comparison.equivalent()) {
        std::cout << "PASS " << name << '\n';
        return true;
    }

    std::cout << "FAIL " << name << '\n';
    if (!same_filename) {
        std::cout << "  package-filename: "
                  << reference.filename().string() << " -> "
                  << result.filename().string() << '\n';
    }
    for (const auto& difference : comparison.differences)
        std::cout << "  " << pkgbuild::parity::format_difference(difference)
                  << '\n';
    return false;
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
            pkgbuild::ExecutionPolicy{identity, {}, 0022});

        const auto environment = selected_environment(identity);
        Workspace workspace(options.work_base, options.keep_work);
        assign_tree(workspace.path(), identity);
        pkgbuild::PosixProcessExecutor executor;

        bool equivalent = true;
        for (const auto& corpus_case : corpus_cases(options.corpus))
            equivalent = run_case(options, executor, identity, environment,
                                  workspace.path(), corpus_case) && equivalent;

        if (options.keep_work)
            std::cout << "WORK " << workspace.path() << '\n';
        return equivalent ? 0 : 1;
    } catch (const pkgbuild::Error& error) {
        std::cerr << "pkgbuild-parity: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "pkgbuild-parity: " << error.what() << '\n';
        return 2;
    }
}
