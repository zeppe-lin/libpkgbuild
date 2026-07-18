#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/stage.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <iostream>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
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
    std::string pattern = "/tmp/libpkgbuild-ar-owner.XXXXXX";
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

std::string shell_quote(const std::filesystem::path& path)
{
    std::string quoted{"'"};
    for (const char character : path.string()) {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    quoted += '\'';
    return quoted;
}

std::uint64_t decimal_field(const std::string& data, std::size_t offset,
                            std::size_t size)
{
    const auto field = data.substr(offset, size);
    const auto first = field.find_first_not_of(' ');
    const auto last = field.find_last_not_of(' ');
    require(first != std::string::npos && last != std::string::npos,
            "empty ar decimal field");
    return std::stoull(field.substr(first, last - first + 1));
}

std::string text_field(const std::string& data, std::size_t offset,
                       std::size_t size)
{
    auto field = data.substr(offset, size);
    while (!field.empty() && field.back() == ' ')
        field.pop_back();
    return field;
}

std::vector<std::pair<std::string, std::string>>
archive_owners(const std::filesystem::path& path)
{
    const auto data = read_file(path);
    require(data.size() >= 8 && data.compare(0, 8, "!<arch>\n") == 0,
            "invalid test ar archive");

    std::vector<std::pair<std::string, std::string>> result;
    std::uint64_t offset = 8;
    while (offset != data.size()) {
        require(offset + 60 <= data.size(), "truncated test ar header");
        require(data.compare(static_cast<std::size_t>(offset + 58), 2,
                             "`\n") == 0,
                "invalid test ar trailer");
        result.emplace_back(
            text_field(data, static_cast<std::size_t>(offset + 28), 6),
            text_field(data, static_cast<std::size_t>(offset + 34), 6));
        const auto size = decimal_field(
            data, static_cast<std::size_t>(offset + 48), 10);
        offset += 60 + size + (size & 1U);
        require(offset <= data.size(), "truncated test ar payload");
    }
    return result;
}

void set_archive_owners(const std::filesystem::path& path,
                        const std::string& uid,
                        const std::string& gid)
{
    require(uid.size() <= 6 && gid.size() <= 6,
            "test ar ownership field is too long");
    std::fstream archive(path, std::ios::in | std::ios::out | std::ios::binary);
    require(static_cast<bool>(archive), "cannot open test ar archive");
    std::string data{std::istreambuf_iterator<char>(archive),
                     std::istreambuf_iterator<char>()};
    require(data.size() >= 8 && data.compare(0, 8, "!<arch>\n") == 0,
            "invalid test ar archive");

    const auto uid_field = uid + std::string(6 - uid.size(), ' ');
    const auto gid_field = gid + std::string(6 - gid.size(), ' ');
    std::uint64_t offset = 8;
    while (offset != data.size()) {
        require(offset + 60 <= data.size(), "truncated test ar header");
        data.replace(static_cast<std::size_t>(offset + 28), 6, uid_field);
        data.replace(static_cast<std::size_t>(offset + 34), 6, gid_field);
        const auto size = decimal_field(
            data, static_cast<std::size_t>(offset + 48), 10);
        offset += 60 + size + (size & 1U);
        require(offset <= data.size(), "truncated test ar payload");
    }

    archive.clear();
    archive.seekp(0);
    archive.write(data.data(), static_cast<std::streamsize>(data.size()));
    require(static_cast<bool>(archive), "cannot rewrite test ar ownership");
}

std::filesystem::path create_archive(
    const std::filesystem::path& root,
    const std::filesystem::path& ar_program)
{
    const auto directory = root / "usr/lib";
    std::filesystem::create_directories(directory);
    const auto member = directory / "member";
    const auto archive = directory / "libowned.a";
    std::filesystem::copy_file("/proc/self/exe", member);

    const auto command = shell_quote(ar_program) + " crU " +
        shell_quote(archive) + " " + shell_quote(member);
    require(std::system(command.c_str()) == 0,
            "cannot create non-deterministic test ar archive");
    std::filesystem::remove(member);
    set_archive_owners(archive, "0", "0");
    return archive;
}

std::filesystem::path create_strip_wrapper(
    const std::filesystem::path& root,
    const std::filesystem::path& strip_program)
{
    const auto wrapper = root / "strip-undeterministic";
    std::ofstream output(wrapper);
    require(static_cast<bool>(output), "cannot create strip wrapper");
    output << "#!/bin/sh\nexec " << shell_quote(strip_program)
           << " -U \"$@\"\n";
    output.close();
    require(chmod(wrapper.c_str(), 0755) == 0,
            "cannot make strip wrapper executable");
    return wrapper;
}

pkgbuild::TransformationReceipt transform_archive(
    const std::filesystem::path& root,
    const std::filesystem::path& strip_wrapper,
    const pkgbuild::ExecutionPolicy& execution,
    const pkgbuild::PosixProcessExecutor& processes)
{
    auto package = pkgbuild::scan_staged_package(root);
    pkgbuild::TransformationPolicy policy;
    policy.compress_manpages = false;
    pkgbuild::PackageTreeTransformer transformer(strip_wrapper, processes);
    pkgbuild::NullEventSink events;
    return transformer.transform(pkgbuild::PackageTransformRequest{
        package, pkgbuild::PackageDefinition{}, policy, execution}, events);
}

void require_root_archive_owners(const std::filesystem::path& archive)
{
    const auto owners = archive_owners(archive);
    require(!owners.empty(), "test ar archive has no members");
    require(std::all_of(owners.begin(), owners.end(), [](const auto& owner) {
                return owner.first == "0" && owner.second == "0";
            }),
            "real build identity leaked into ar member ownership");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: archive-strip-ownership-test STRIP AR\n";
        return 2;
    }

    std::filesystem::path temporary;
    try {
        temporary = temporary_directory();
        const pkgbuild::PosixProcessExecutor processes;
        const auto wrapper = create_strip_wrapper(temporary, argv[1]);
        const auto package_root = temporary / "package";
        std::filesystem::create_directories(package_root);
        const auto archive = create_archive(package_root, argv[2]);
        require_root_archive_owners(archive);

        pkgbuild::ExecutionPolicy execution;
        execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
        if (geteuid() == 0) {
            execution.identity = nobody_identity();
            assign_tree(package_root, *execution.identity);
        }

        const auto receipt = transform_archive(
            package_root, wrapper, execution, processes);
        require(receipt.changes.size() == 1,
                "archive transformation count is wrong");
        require_root_archive_owners(archive);

        std::filesystem::remove_all(temporary);
        std::cout << "archive strip ownership: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!temporary.empty())
            std::filesystem::remove_all(temporary);
        std::cerr << "archive strip ownership: " << error.what() << '\n';
        return 1;
    }
}
