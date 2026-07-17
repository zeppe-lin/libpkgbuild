#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/stage.hpp>

#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
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
    std::string pattern = "/tmp/libpkgbuild-strip.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* created = mkdtemp(storage.data());
    if (!created)
        fail("cannot create temporary directory");
    (void)chmod(created, 0755);
    return created;
}

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    if (getpwnam_r("nobody", &record, buffer.data(), buffer.size(), &result) != 0 ||
        !result)
        fail("cannot resolve nobody build identity");

    int count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &count);
    std::vector<gid_t> groups(static_cast<std::size_t>(count));
    if (count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(), &count) < 0)
        fail("cannot resolve nobody supplementary groups");
    groups.resize(static_cast<std::size_t>(count));
    return {record.pw_uid, record.pw_gid, std::move(groups),
            record.pw_dir ? record.pw_dir : "/", record.pw_name};
}

void assign_tree(const std::filesystem::path& root,
                 const pkgbuild::BuildIdentity& identity)
{
    require(lchown(root.c_str(), identity.uid, identity.gid) == 0,
            "cannot assign package root");
    for (const auto& item : std::filesystem::recursive_directory_iterator(root))
        require(lchown(item.path().c_str(), identity.uid, identity.gid) == 0,
                "cannot assign package tree");
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        fail("cannot read file: " + path.string());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

const pkgbuild::StagedEntry& find_entry(const pkgbuild::StagedPackage& package,
                                        const std::filesystem::path& path)
{
    for (const auto& entry : package.entries) {
        if (entry.path == path)
            return entry;
    }
    fail("missing staged entry: " + path.string());
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: strip-transform-test STRIP\n";
        return 2;
    }

    std::filesystem::path root;
    try {
        root = temporary_directory();
        const auto bin = root / "usr/bin";
        std::filesystem::create_directories(bin);
        std::filesystem::copy_file("/proc/self/exe", bin / "tool");
        require(link((bin / "tool").c_str(), (bin / "tool-alias").c_str()) == 0,
                "cannot create binary hardlink");
        std::filesystem::copy_file("/proc/self/exe", bin / "keep");

        const auto keep_before = read_file(bin / "keep");
        const auto size_before = std::filesystem::file_size(bin / "tool");
        auto package = pkgbuild::scan_staged_package(root);

        pkgbuild::PackageDefinition definition;
        definition.strip_exclusions = {"usr/bin/keep"};
        pkgbuild::TransformationPolicy policy;
        policy.compress_manpages = false;
        pkgbuild::ExecutionPolicy execution;
        execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
        if (geteuid() == 0) {
            execution.identity = nobody_identity();
            assign_tree(root, *execution.identity);
        }

        pkgbuild::PosixProcessExecutor processes;
        pkgbuild::PackageTreeTransformer transformer(argv[1], processes);
        pkgbuild::NullEventSink events;
        const auto receipt = transformer.transform(
            pkgbuild::PackageTransformRequest{
                package, definition, policy, execution},
            events);

        const auto size_after = std::filesystem::file_size(bin / "tool");
        require(size_after < size_before, "binary was not stripped");
        require(read_file(bin / "keep") == keep_before,
                "excluded binary was changed");

        struct stat primary {};
        struct stat alias {};
        require(lstat((bin / "tool").c_str(), &primary) == 0,
                "cannot inspect stripped binary");
        require(lstat((bin / "tool-alias").c_str(), &alias) == 0,
                "cannot inspect stripped binary alias");
        require(primary.st_dev == alias.st_dev && primary.st_ino == alias.st_ino,
                "stripping fractured the hardlink group");

        require(find_entry(package, "usr/bin/tool").size == size_after,
                "staged size was not updated after stripping");
        require(find_entry(package, "usr/bin/tool-alias").size == size_after,
                "staged hardlink size was not updated");
        require(receipt.changes.size() == 1,
                "unexpected strip transformation count");
        require(receipt.changes.front().kind ==
                    pkgbuild::TransformationKind::strip_binary,
                "strip receipt has the wrong action");
        require(receipt.changes.front().bytes_before == size_before &&
                    receipt.changes.front().bytes_after == size_after,
                "strip receipt sizes are wrong");

        for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
            require(item.path().filename().string().rfind(".pkgbuild-", 0) != 0,
                    "strip temporary or backup leaked into package tree");
        }

        std::filesystem::remove_all(root);
        std::cout << "binary transformation: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "binary transformation: " << error.what() << '\n';
        return 1;
    }
}
