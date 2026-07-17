#include <pkgbuild/backends/curl.hpp>
#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/engine.hpp>
#include <pkgbuild/error.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <array>
#include <cstdlib>
#include <map>

#ifndef PKGBUILD_PKGFILE_HELPER
#define PKGBUILD_PKGFILE_HELPER "/usr/libexec/pkgbuild-pkgfile"
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
           << "  -c, --config FILE        source legacy pkgmk configuration\n"
           << "      --source-dir DIR     source cache directory\n"
           << "      --package-dir DIR    package output directory\n"
           << "      --work-dir DIR       temporary work directory\n"
           << "      --helper FILE        pkgfile/0 worker path\n"
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
        std::filesystem::path recipe_dir = std::filesystem::current_path();
        std::optional<std::filesystem::path> config_file;
        std::optional<std::filesystem::path> source_dir;
        std::optional<std::filesystem::path> package_dir;
        std::optional<std::filesystem::path> work_dir;
        std::filesystem::path helper = PKGBUILD_PKGFILE_HELPER;
        bool download = false;
        bool keep_work = false;

        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "-d" || option == "--download") {
                download = true;
            } else if (option == "-k" || option == "--keep-work") {
                keep_work = true;
            } else if (option == "-c" || option == "--config") {
                config_file = require_argument(i, argc, argv);
            } else if (option == "--source-dir") {
                source_dir = require_argument(i, argc, argv);
            } else if (option == "--package-dir") {
                package_dir = require_argument(i, argc, argv);
            } else if (option == "--work-dir") {
                work_dir = require_argument(i, argc, argv);
            } else if (option == "--helper") {
                helper = require_argument(i, argc, argv);
            } else if (option == "-h" || option == "--help") {
                usage(argv[0], 0);
            } else if (!option.empty() && option[0] == '-') {
                usage(argv[0], 2);
            } else {
                recipe_dir = option;
            }
        }

        recipe_dir = std::filesystem::absolute(recipe_dir);
        pkgbuild::BuildPaths paths{
            recipe_dir,
            source_dir ? std::filesystem::absolute(*source_dir) : recipe_dir,
            package_dir ? std::filesystem::absolute(*package_dir) : recipe_dir,
            work_dir ? std::filesystem::absolute(*work_dir) : recipe_dir / "work",
        };

        pkgbuild::PosixProcessExecutor processes;
        pkgbuild::PkgfileDefinitionLoader definitions(helper, processes);
        pkgbuild::CurlDownloader downloader;
        pkgbuild::LibarchiveBackend archives;
        pkgbuild::PosixShellRecipeRunner recipes(helper, processes);
        pkgbuild::Services services{
            definitions,
            downloader,
            archives,
            recipes,
            archives,
        };
        pkgbuild::Engine engine(services);
        TerminalEvents events;

        pkgbuild::BuildRequest request{
            pkgbuild::DefinitionRequest{
                paths,
                config_file,
                pkgbuild::ArchiveSpec{},
                pkgbuild::ExecutionPolicy{
                    std::nullopt,
                    selected_environment(),
                    0022,
                },
            },
            download,
            keep_work,
        };

        const auto receipt = engine.build(request, events);
        std::cout << receipt.package << '\n';
        return 0;
    } catch (const pkgbuild::Error& error) {
        std::cerr << "pkgbuild-example: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "pkgbuild-example: unexpected error: "
                  << error.what() << '\n';
        return 1;
    }
}
