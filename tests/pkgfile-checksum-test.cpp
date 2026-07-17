#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/backends/posix.hpp>
#include <pkgbuild/error.hpp>

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

void require(bool value, const std::string& message)
{
    if (!value)
        fail(message);
}

template<typename Function>
void require_invalid(Function function)
{
    try {
        function();
    } catch (const pkgbuild::Error& error) {
        require(error.code() == pkgbuild::ErrorCode::invalid_definition,
                "unexpected error code for malformed checksum manifest");
        return;
    }
    fail("expected invalid checksum manifest");
}

std::filesystem::path temporary_directory()
{
    std::string pattern = "/tmp/libpkgbuild-pkgfile-md5.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* result = mkdtemp(storage.data());
    if (!result)
        fail("mkdtemp failed");
    (void)chmod(result, 0755);
    return result;
}

pkgbuild::BuildIdentity nobody_identity()
{
    std::vector<char> buffer(16384);
    struct passwd record {};
    struct passwd* result = nullptr;
    if (getpwnam_r("nobody", &record, buffer.data(), buffer.size(), &result) !=
            0 ||
        !result)
        fail("nobody user is required for root checksum tests");

    int count = 0;
    (void)getgrouplist(record.pw_name, record.pw_gid, nullptr, &count);
    std::vector<gid_t> groups(static_cast<std::size_t>(count));
    if (count != 0 &&
        getgrouplist(record.pw_name, record.pw_gid, groups.data(), &count) < 0)
        fail("cannot read nobody supplementary groups");
    groups.resize(static_cast<std::size_t>(count));

    return {record.pw_uid, record.pw_gid, std::move(groups),
            record.pw_dir ? record.pw_dir : "/", record.pw_name};
}

pkgbuild::DefinitionRequest request_for(const std::filesystem::path& recipe)
{
    pkgbuild::ExecutionPolicy execution;
    execution.environment = {{"PATH", "/usr/bin:/bin"}, {"LANG", "C"}};
    if (geteuid() == 0)
        execution.identity = nobody_identity();
    return {
        {recipe, recipe, recipe.parent_path() / "packages",
         recipe.parent_path() / "work"},
        std::nullopt,
        {},
        std::move(execution),
    };
}

std::filesystem::path make_recipe(const std::filesystem::path& root,
                                  const std::string& name,
                                  const std::string& sources,
                                  const std::optional<std::string>& manifest)
{
    const auto recipe = root / name;
    std::filesystem::create_directory(recipe);
    (void)chmod(recipe.c_str(), 0755);
    std::ofstream pkgfile(recipe / "Pkgfile");
    pkgfile << "name=" << name << "\nversion=1\nrelease=1\nsource=\""
            << sources << "\"\nbuild() { :; }\n";
    pkgfile.close();
    (void)chmod((recipe / "Pkgfile").c_str(), 0644);
    if (manifest) {
        std::ofstream checksum(recipe / ".md5sum");
        checksum << *manifest;
        checksum.close();
        (void)chmod((recipe / ".md5sum").c_str(), 0644);
    }
    return recipe;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: pkgfile-checksum-test HELPER\n";
        return 2;
    }

    std::filesystem::path root;
    try {
        root = temporary_directory();
        pkgbuild::PosixProcessExecutor processes;
        pkgbuild::PkgfileDefinitionLoader loader(argv[1], processes);
        pkgbuild::NullEventSink events;

        const auto valid = make_recipe(
            root, "valid",
            "foo.tar.gz renamed.patch::https://example.invalid/original.patch",
            "# generated manifest\n"
            "D41D8CD98F00B204E9800998ECF8427E  renamed.patch\n"
            "900150983cd24fb0d6963f7d28e17f72  foo.tar.gz\n");
        const auto definition = loader.load(request_for(valid), events);
        require(definition.sources.size() == 2, "source count changed");
        require(definition.sources[0].digests[0].hexadecimal ==
                    "900150983cd24fb0d6963f7d28e17f72",
                "first checksum was not attached by local filename");
        require(definition.sources[1].local_name == "renamed.patch",
                "renamed source identity changed");
        require(definition.sources[1].digests[0].hexadecimal ==
                    "d41d8cd98f00b204e9800998ecf8427e",
                "renamed source checksum was not normalized");

        const auto source_less =
            make_recipe(root, "source-less", "", std::nullopt);
        require(loader.load(request_for(source_less), events).sources.empty(),
                "source-less package was rejected");

        const auto missing =
            make_recipe(root, "missing", "foo", std::nullopt);
        require_invalid([&] { (void)loader.load(request_for(missing), events); });

        const auto absent = make_recipe(
            root, "absent", "foo bar",
            "900150983cd24fb0d6963f7d28e17f72  foo\n");
        require_invalid([&] { (void)loader.load(request_for(absent), events); });

        const auto extra = make_recipe(
            root, "extra", "foo",
            "900150983cd24fb0d6963f7d28e17f72  foo\n"
            "d41d8cd98f00b204e9800998ecf8427e  bar\n");
        require_invalid([&] { (void)loader.load(request_for(extra), events); });

        const auto malformed = make_recipe(
            root, "malformed", "foo", "not-a-digest  foo\n");
        require_invalid(
            [&] { (void)loader.load(request_for(malformed), events); });

        const auto duplicate = make_recipe(
            root, "duplicate", "foo",
            "900150983cd24fb0d6963f7d28e17f72  foo\n"
            "d41d8cd98f00b204e9800998ecf8427e  foo\n");
        require_invalid(
            [&] { (void)loader.load(request_for(duplicate), events); });

        const auto ambiguous = make_recipe(
            root, "ambiguous", "one/foo.patch two/foo.patch",
            "900150983cd24fb0d6963f7d28e17f72  foo.patch\n");
        require_invalid(
            [&] { (void)loader.load(request_for(ambiguous), events); });

        const auto stale = make_recipe(
            root, "stale", "",
            "900150983cd24fb0d6963f7d28e17f72  old.tar.gz\n");
        require_invalid([&] { (void)loader.load(request_for(stale), events); });

        std::filesystem::remove_all(root);
        std::cout << "pkgfile checksums: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "pkgfile checksums: " << error.what() << '\n';
        return 1;
    }
}
