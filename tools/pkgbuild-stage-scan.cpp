#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include "stage.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: pkgbuild-stage-scan ROOT\n";
        return 2;
    }

    try {
        const auto package =
            pkgbuild::scan_staged_package(std::filesystem::path(argv[1]));
        const auto manifest =
            pkgbuild::detail::serialize_staged_manifest(package);
        std::cout.write(manifest.data(),
                        static_cast<std::streamsize>(manifest.size()));
        if (!std::cout)
            throw pkgbuild::Error(pkgbuild::ErrorCode::filesystem_failed,
                                  "cannot write staged metadata");
        return 0;
    } catch (const pkgbuild::Error& error) {
        std::cerr << "pkgbuild-stage-scan: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "pkgbuild-stage-scan: unexpected error: "
                  << error.what() << '\n';
        return 1;
    }
}
