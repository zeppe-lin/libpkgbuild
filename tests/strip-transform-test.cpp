#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/definition.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include <libpkgsource/pkgfile_backend.h>

#include <array>
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

template<typename Function>
void require_transformation_error(Function function)
{
    try {
        function();
    } catch (const pkgbuild::Error& error) {
        require(error.code() == pkgbuild::ErrorCode::transformation_failed,
                "unexpected transformation error code");
        return;
    }
    fail("expected transformation failure");
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

void write_sectionless_elf(const std::filesystem::path& path,
                           std::uint16_t type)
{
    std::array<unsigned char, 64> header{};
    header[0] = 0x7f;
    header[1] = 'E';
    header[2] = 'L';
    header[3] = 'F';
    header[4] = 2;
    header[5] = 1;
    header[6] = 1;
    header[16] = static_cast<unsigned char>(type & 0xffU);
    header[17] = static_cast<unsigned char>((type >> 8U) & 0xffU);
    header[18] = 0x3e;
    header[20] = 1;
    header[52] = 64;

    std::ofstream output(path, std::ios::binary);
    if (!output)
        fail("cannot create sectionless ELF fixture");
    output.write(reinterpret_cast<const char*>(header.data()),
                 static_cast<std::streamsize>(header.size()));
    if (!output)
        fail("cannot write sectionless ELF fixture");
}

void write_file(const std::filesystem::path& path,
                const std::string& contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output)
        fail("cannot create file: " + path.string());
    output << contents;
    if (!output)
        fail("cannot write file: " + path.string());
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
    std::filesystem::path definition_root;
    try {
        root = temporary_directory();
        const auto bin = root / "usr/bin";
        std::filesystem::create_directories(bin);
        std::filesystem::copy_file("/proc/self/exe", bin / "tool");
        require(link((bin / "tool").c_str(), (bin / "tool-alias").c_str()) == 0,
                "cannot create binary hardlink");
        std::filesystem::copy_file("/proc/self/exe", bin / "keep");
        const auto lib = root / "usr/lib";
        std::filesystem::create_directories(lib);
        std::ofstream(lib / "thin.a", std::ios::binary) << "!<thin>\nexternal.o/\n";
        const auto firmware = root / "lib/firmware";
        std::filesystem::create_directories(firmware);
        write_sectionless_elf(firmware / "exec.mbn", 2);
        write_sectionless_elf(firmware / "shared.mbn", 3);

        const auto keep_before = read_file(bin / "keep");
        const auto thin_before = read_file(lib / "thin.a");
        const auto sectionless_exec_before = read_file(firmware / "exec.mbn");
        const auto sectionless_shared_before = read_file(firmware / "shared.mbn");
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
        require(read_file(lib / "thin.a") == thin_before,
                "thin archive crossed the staged-tree boundary");
        require(read_file(firmware / "exec.mbn") == sectionless_exec_before,
                "sectionless executable image was stripped");
        require(read_file(firmware / "shared.mbn") == sectionless_shared_before,
                "sectionless shared-object image was stripped");

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

        definition_root = temporary_directory();
        const auto definition_origin = definition_root / "ere-definition";
        write_file(definition_origin / "Pkgfile",
                   "name=ere-definition\n"
                   "version=1\n"
                   "release=1\n"
                   "build() { :; }\n");
        write_file(definition_origin / ".nostrip",
                   "^usr/(bin|sbin)/keep-ere$\n");

        pkgsource::pkgfile_backend source_backend;
        auto snapshot = source_backend.inspect({
            pkgsource::source_location(definition_origin), std::nullopt, {}});
        pkgbuild::AcceptedBuildPolicy accepted;
        accepted.transformations.compress_manpages = false;
        const auto build_definition = pkgbuild::derive_definition(
            std::move(snapshot), accepted);

        const auto ere_root = definition_root / "package";
        std::filesystem::create_directories(ere_root / "usr/bin");
        std::filesystem::copy_file("/proc/self/exe",
                                   ere_root / "usr/bin/keep-ere");
        if (execution.identity)
            assign_tree(ere_root, *execution.identity);
        const auto ere_before = read_file(ere_root / "usr/bin/keep-ere");
        auto ere_package = pkgbuild::scan_staged_package(ere_root);
        const auto ere_receipt = transformer.transform_definition(
            pkgbuild::DefinitionTransformRequest{
                ere_package, build_definition, execution},
            events);
        require(read_file(ere_root / "usr/bin/keep-ere") == ere_before,
                "POSIX ERE strip exclusion was interpreted as BRE");
        require(ere_receipt.changes.empty(),
                "excluded ERE path produced a strip receipt");

        for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
            require(item.path().filename().string().rfind(".pkgbuild-", 0) != 0,
                    "strip temporary or backup leaked into package tree");
        }

        const auto rollback = root / "rollback";
        std::filesystem::create_directories(rollback / "usr/bin");
        std::filesystem::copy_file("/proc/self/exe", rollback / "usr/bin/tool");
        require(link((rollback / "usr/bin/tool").c_str(),
                     (rollback / "usr/bin/tool-alias").c_str()) == 0,
                "cannot create rollback hardlink");
        if (execution.identity)
            assign_tree(rollback, *execution.identity);
        const auto rollback_before = read_file(rollback / "usr/bin/tool");
        auto rollback_package = pkgbuild::scan_staged_package(rollback);
        pkgbuild::PackageTreeTransformer failing("/bin/false", processes);
        require_transformation_error([&] {
            (void)failing.transform(pkgbuild::PackageTransformRequest{
                rollback_package, pkgbuild::PackageDefinition{}, policy,
                execution}, events);
        });
        require(read_file(rollback / "usr/bin/tool") == rollback_before,
                "failed strip changed the original payload");
        struct stat rollback_primary {};
        struct stat rollback_alias {};
        require(lstat((rollback / "usr/bin/tool").c_str(), &rollback_primary) == 0 &&
                    lstat((rollback / "usr/bin/tool-alias").c_str(),
                          &rollback_alias) == 0,
                "failed strip removed a hardlink member");
        require(rollback_primary.st_ino == rollback_alias.st_ino,
                "failed strip fractured the original hardlink group");

        auto disabled_package = pkgbuild::scan_staged_package(rollback);
        auto disabled = policy;
        disabled.strip_binaries = false;
        const auto disabled_receipt = transformer.transform(
            pkgbuild::PackageTransformRequest{
                disabled_package, pkgbuild::PackageDefinition{}, disabled,
                execution}, events);
        require(disabled_receipt.changes.empty(),
                "disabled stripping produced transformations");

        std::filesystem::remove_all(root);
        std::filesystem::remove_all(definition_root);
        std::cout << "binary transformation: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        if (!definition_root.empty())
            std::filesystem::remove_all(definition_root);
        std::cerr << "binary transformation: " << error.what() << '\n';
        return 1;
    }
}
