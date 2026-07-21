#include <pkgbuild/backends/curl.hpp>
#include <pkgbuild/backends/fakeroot.hpp>
#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/definition.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>

#include <libpkgsource/error.h>
#include <libpkgsource/pkgfile_backend.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <array>
#include <clocale>
#include <cstdlib>
#include <map>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
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

class TerminalEvents final : public pkgbuild::EventSink {
public:
    void emit(const pkgbuild::Event& event) override
    {
        std::ostream& stream =
            event.kind == pkgbuild::EventKind::info ? std::cout : std::cerr;
        stream << "=======> ";
        if (event.kind == pkgbuild::EventKind::warning)
            stream << "WARNING: ";
        stream << event.message << '\n';
    }
};

[[noreturn]] void usage(const char* program, int status)
{
    std::ostream& stream = status == 0 ? std::cout : std::cerr;
    stream << "Usage: " << program << " [options] [recipe-directory]\n"
           << "\n"
           << "Options:\n"
           << "  -d, --download           download missing URI sources\n"
           << "  -k, --keep-work          keep the work directory\n"
           << "      --archive-format FMT gnutar, pax, ustar, or v7\n"
           << "      --compression MODE   gz, bz2, xz, lz, or zst\n"
           << "      --no-strip           disable binary stripping\n"
           << "      --no-compress-manpages  disable man-page compression\n"
           << "      --source-dir DIR     source cache directory\n"
           << "      --package-dir DIR    package output directory\n"
           << "      --work-dir DIR       private workspace base\n"
           << "      --workspace-dir DIR  exact private workspace path\n"
           << "      --tmp-dir DIR        controlled recipe temporary directory\n"
           << "      --helper FILE        pkgfile/0 worker path\n"
           << "      --scanner FILE       staged metadata scanner path\n"
           << "      --fakeroot FILE      fakeroot frontend path\n"
           << "      --build-user USER    execute Pkgfile as USER\n"
           << "      --strip FILE         binary stripping program\n"
           << "      --check-footprint    compare captured footprint\n"
           << "      --write-footprint FILE  replace footprint atomically\n"
           << "  -h, --help               show this help\n";
    std::exit(status);
}


std::map<std::string, std::string> selected_environment()
{
    static const std::array<const char*, 9> names = {
        "PATH", "LANG", "LC_ALL", "MAKEFLAGS", "CFLAGS",
        "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "PKG_CONFIG_PATH",
    };
    std::map<std::string, std::string> result;
    for (const char* name : names) {
        if (const char* value = std::getenv(name))
            result.emplace(name, value);
    }
    return result;
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
        record.pw_dir ? record.pw_dir : "/",
        record.pw_name,
    };
}

pkgsource::worker_identity source_identity(
    const pkgbuild::BuildIdentity& identity)
{
    return pkgsource::worker_identity{
        identity.uid,
        identity.gid,
        identity.user,
        identity.home,
    };
}

std::string require_argument(int& index, int argc, char** argv)
{
    if (++index >= argc)
        usage(argv[0], 2);
    return argv[index];
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (std::setlocale(LC_ALL, "") == nullptr)
            throw std::runtime_error("cannot activate process locale");

        std::filesystem::path recipe_dir = std::filesystem::current_path();
        std::optional<std::filesystem::path> source_dir;
        std::optional<std::filesystem::path> package_dir;
        std::optional<std::filesystem::path> work_dir;
        std::optional<std::filesystem::path> workspace_directory;
        std::optional<std::filesystem::path> temporary_directory;
        std::filesystem::path helper = PKGBUILD_PKGFILE_HELPER;
        std::filesystem::path scanner = PKGBUILD_STAGE_SCANNER;
        std::filesystem::path fakeroot = PKGBUILD_FAKEROOT;
        std::filesystem::path strip = PKGBUILD_STRIP;
        std::optional<std::string> build_user;
        pkgbuild::AcceptedBuildPolicy policy;
        bool download = false;
        bool keep_work = false;

        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "-d" || option == "--download") {
                download = true;
            } else if (option == "-k" || option == "--keep-work") {
                keep_work = true;
            } else if (option == "--archive-format") {
                policy.archive.format = pkgbuild::archive_format_from_string(
                    require_argument(i, argc, argv));
            } else if (option == "--compression") {
                policy.archive.compression = pkgbuild::compression_from_string(
                    require_argument(i, argc, argv));
            } else if (option == "--no-strip") {
                policy.transformations.strip_binaries = false;
            } else if (option == "--no-compress-manpages") {
                policy.transformations.compress_manpages = false;
            } else if (option == "--source-dir") {
                source_dir = require_argument(i, argc, argv);
            } else if (option == "--package-dir") {
                package_dir = require_argument(i, argc, argv);
            } else if (option == "--work-dir") {
                work_dir = require_argument(i, argc, argv);
            } else if (option == "--workspace-dir") {
                workspace_directory = require_argument(i, argc, argv);
            } else if (option == "--tmp-dir") {
                temporary_directory = require_argument(i, argc, argv);
            } else if (option == "--helper") {
                helper = require_argument(i, argc, argv);
            } else if (option == "--scanner") {
                scanner = require_argument(i, argc, argv);
            } else if (option == "--fakeroot") {
                fakeroot = require_argument(i, argc, argv);
            } else if (option == "--build-user") {
                build_user = require_argument(i, argc, argv);
            } else if (option == "--strip") {
                strip = require_argument(i, argc, argv);
            } else if (option == "--check-footprint") {
                policy.footprint.action = pkgbuild::FootprintAction::compare;
            } else if (option == "--write-footprint") {
                policy.footprint.action = pkgbuild::FootprintAction::write;
                policy.footprint.manifest =
                    std::filesystem::absolute(require_argument(i, argc, argv));
            } else if (option == "-h" || option == "--help") {
                usage(argv[0], 0);
            } else if (!option.empty() && option[0] == '-') {
                usage(argv[0], 2);
            } else {
                recipe_dir = option;
            }
        }

        recipe_dir = std::filesystem::absolute(recipe_dir);
        const auto source_cache =
            source_dir ? std::filesystem::absolute(*source_dir) : recipe_dir;
        const auto package_output =
            package_dir ? std::filesystem::absolute(*package_dir) : recipe_dir;
        const auto work_base = work_dir
            ? std::filesystem::absolute(*work_dir)
            : recipe_dir / "work";

        std::optional<pkgbuild::BuildIdentity> identity;
        if (build_user)
            identity = build_identity(*build_user);
        const auto environment = selected_environment();

        pkgsource::evaluation_policy evaluation;
        evaluation.file_creation_mask = 0022;
        if (identity)
            evaluation.identity = source_identity(*identity);

        pkgsource::pkgfile_backend source_backend;
        auto snapshot = source_backend.inspect({
            pkgsource::source_location(recipe_dir),
            std::nullopt,
            std::move(evaluation),
        });
        auto definition = pkgbuild::derive_definition(
            std::move(snapshot), std::move(policy));

        pkgbuild::PosixProcessExecutor processes;
        pkgbuild::CurlDownloader downloader;
        pkgbuild::OpenSslSourceVerifier verifier;
        pkgbuild::LibarchiveBackend archives;
        pkgbuild::FakerootPkgfileRecipeRunner recipes(
            fakeroot, helper, scanner, processes);
        pkgbuild::PackageTreeTransformer transformer(strip, processes);
        pkgbuild::BuildServices services{
            downloader,
            verifier,
            archives,
            recipes,
            transformer,
            archives,
        };
        pkgbuild::Engine engine(services);
        TerminalEvents events;

        pkgbuild::BuildEnvironment build_environment{
            source_cache,
            package_output,
            work_base,
            pkgbuild::ExecutionPolicy{
                identity,
                environment,
                0022,
                temporary_directory
                    ? std::optional<std::filesystem::path>(
                        std::filesystem::absolute(*temporary_directory))
                    : std::nullopt,
            },
            download,
            keep_work,
            workspace_directory
                ? std::optional<std::filesystem::path>(
                    std::filesystem::absolute(*workspace_directory))
                : std::nullopt,
        };

        const auto receipt =
            engine.build(definition, build_environment, events);
        const auto& built = receipt.definition;
        const auto& package = built.identity();
        std::cout << "artifact\t" << receipt.archive.output << '\n'
                  << "source-snapshot\t"
                  << pkgsource::to_string(
                         built.source_snapshot_fingerprint().algorithm())
                  << ':' << built.source_snapshot_fingerprint().hex() << '\n'
                  << "package\t" << package.name << '#' << package.version
                  << '-' << package.release << '\n'
                  << "archive\t" << pkgbuild::to_string(
                         receipt.archive.archive.format)
                  << '/' << pkgbuild::to_string(
                         receipt.archive.archive.compression) << '\n'
                  << "bytes\t" << receipt.archive.bytes_written << '\n';
        return 0;
    } catch (const pkgsource::error& error) {
        std::cerr << "pkgbuild-example: source inspection: "
                  << error.what() << '\n';
        return 1;
    } catch (const pkgbuild::Error& error) {
        std::cerr << "pkgbuild-example: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "pkgbuild-example: unexpected error: "
                  << error.what() << '\n';
        return 1;
    }
}
