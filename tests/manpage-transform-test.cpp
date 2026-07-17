#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/stage.hpp>
#include <pkgbuild/backends/posix.hpp>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>

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
    std::string pattern = "/tmp/libpkgbuild-manpage.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* created = mkdtemp(storage.data());
    if (!created)
        fail("cannot create temporary directory");
    return created;
}

std::string read_gzip(const std::filesystem::path& path)
{
    gzFile input = gzopen(path.c_str(), "rb");
    if (!input)
        fail("cannot open gzip output");
    std::string result;
    char buffer[4096];
    for (;;) {
        const int count = gzread(input, buffer, sizeof(buffer));
        if (count > 0) {
            result.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0)
            break;
        (void)gzclose(input);
        fail("cannot read gzip output");
    }
    require(gzclose(input) == Z_OK, "cannot close gzip output");
    return result;
}

const pkgbuild::StagedEntry& entry(const pkgbuild::StagedPackage& package,
                                   std::string_view path)
{
    for (const auto& candidate : package.entries) {
        if (candidate.path.generic_string() == path)
            return candidate;
    }
    fail("missing staged entry: " + std::string(path));
}

} // namespace

int main()
{
    std::filesystem::path root;
    try {
        root = temporary_directory();
        const auto man = root / "usr/share/man/man1";
        std::filesystem::create_directories(man);
        std::ofstream(man / "tool.1") << "manual payload\n";
        require(link((man / "tool.1").c_str(), (man / "tool-alias.1").c_str()) == 0,
                "cannot create man page hardlink");
        require(symlink("tool.1", (man / "tool-link.1").c_str()) == 0,
                "cannot create man page symlink");

        auto package = pkgbuild::scan_staged_package(root);
        pkgbuild::PosixProcessExecutor processes;
        pkgbuild::PackageTreeTransformer transformer("/usr/bin/strip", processes);
        pkgbuild::NullEventSink events;
        pkgbuild::PackageDefinition definition;
        pkgbuild::TransformationPolicy policy;
        policy.strip_binaries = false;
        const auto receipt = transformer.transform(
            pkgbuild::PackageTransformRequest{
                package, definition, policy, pkgbuild::ExecutionPolicy{}},
            events);

        require(!std::filesystem::exists(man / "tool.1"),
                "uncompressed man page survived");
        require(!std::filesystem::exists(man / "tool-alias.1"),
                "uncompressed hardlink survived");
        require(read_gzip(man / "tool.1.gz") == "manual payload\n",
                "compressed man page payload changed");

        struct stat primary {};
        struct stat alias {};
        require(lstat((man / "tool.1.gz").c_str(), &primary) == 0,
                "cannot inspect compressed man page");
        require(lstat((man / "tool-alias.1.gz").c_str(), &alias) == 0,
                "cannot inspect compressed hardlink");
        require(primary.st_dev == alias.st_dev && primary.st_ino == alias.st_ino,
                "compressed hardlink group was split");

        require(std::filesystem::is_symlink(man / "tool-link.1.gz"),
                "man page symlink was not renamed");
        require(std::filesystem::read_symlink(man / "tool-link.1.gz") ==
                    "tool.1.gz",
                "man page symlink target was not rewritten");

        const auto& hardlink = entry(package, "usr/share/man/man1/tool.1.gz");
        require(hardlink.hardlink_target ==
                    std::filesystem::path("usr/share/man/man1/tool-alias.1.gz"),
                "staged hardlink target was not rewritten");
        const auto& symlink_entry = entry(package, "usr/share/man/man1/tool-link.1.gz");
        require(symlink_entry.symlink_target == "tool.1.gz",
                "staged symlink target was not rewritten");
        require(receipt.changes.size() == 2,
                "unexpected man page transformation receipt");

        std::ifstream gzip(man / "tool.1.gz", std::ios::binary);
        unsigned char header[8] {};
        gzip.read(reinterpret_cast<char*>(header), sizeof(header));
        require(gzip.gcount() == static_cast<std::streamsize>(sizeof(header)),
                "gzip header is truncated");
        require(header[4] == 0 && header[5] == 0 && header[6] == 0 &&
                    header[7] == 0,
                "gzip header contains a nondeterministic timestamp");

        std::filesystem::remove_all(root);
        std::cout << "man page transformation: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "man page transformation: " << error.what() << '\n';
        return 1;
    }
}
