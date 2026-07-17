#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/event.hpp>
#include <pkgbuild/stage.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

std::filesystem::path temporary_directory()
{
    std::string pattern = "/tmp/libpkgbuild-archive.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* created = mkdtemp(storage.data());
    if (!created)
        fail("cannot create temporary directory");
    return created;
}

void expect_archive_failure(const pkgbuild::StagedPackage& package,
                            const std::filesystem::path& output)
{
    pkgbuild::LibarchiveBackend archives;
    pkgbuild::NullEventSink events;
    try {
        (void)archives.write(
            pkgbuild::PackageWriteRequest{package, output, {}}, events);
    } catch (const pkgbuild::Error& error) {
        require(error.code() == pkgbuild::ErrorCode::archive_failed,
                "unexpected error code for changed payload");
        return;
    }
    fail("changed staged payload was accepted");
}

} // namespace

int main()
{
    std::filesystem::path root;
    try {
        root = temporary_directory();
        const auto package_root = root / "pkg";
        std::filesystem::create_directories(package_root / "usr/bin");
        const auto payload = package_root / "usr/bin/tool";
        std::ofstream(payload) << "first\n";

        const auto staged = pkgbuild::scan_staged_package(package_root);
        std::ofstream(payload, std::ios::app) << "second\n";
        expect_archive_failure(staged, root / "changed.pkg.tar.gz");

        std::filesystem::remove_all(package_root);
        std::filesystem::create_directories(package_root / "usr/bin");
        std::ofstream(package_root / "usr/bin/tool") << "stable\n";
        const auto swapped = pkgbuild::scan_staged_package(package_root);

        const auto outside = root / "outside";
        std::filesystem::create_directories(outside);
        std::ofstream(outside / "tool") << "stable\n";
        std::filesystem::rename(package_root / "usr/bin",
                                package_root / "usr/bin.real");
        std::filesystem::create_directory_symlink(outside,
                                                  package_root / "usr/bin");
        expect_archive_failure(swapped, root / "symlink.pkg.tar.gz");

        std::filesystem::remove_all(root);
        std::cout << "archive integrity: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "archive integrity: " << error.what() << '\n';
        return 1;
    }
}
