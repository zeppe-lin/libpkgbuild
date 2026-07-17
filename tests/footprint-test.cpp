#include <pkgbuild/error.hpp>
#include <pkgbuild/footprint.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message)
{
    std::cerr << "footprint-test: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/pkgbuild-footprint.XXXXXX";
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        char* result = mkdtemp(storage.data());
        if (result == nullptr)
            fail("cannot create temporary directory");
        path_ = result;
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

pkgbuild::Footprint sample()
{
    using Type = pkgbuild::StagedEntryType;
    return {{
        {"dev/nullish", Type::character_device, 0600, 0, 0, std::nullopt},
        {"run/pipe", Type::fifo, 0644, 1, 2, std::nullopt},
        {"usr", Type::directory, 0755, 0, 0, std::nullopt},
        {"usr/bin/link", Type::symbolic_link, 0777, 0, 0,
         std::string("tool with space")},
        {"usr/bin/tool with space", Type::regular_file, 04755, 123, 456,
         std::nullopt},
    }};
}

void expect_error(const std::filesystem::path& path, const char* label)
{
    try {
        (void)pkgbuild::read_footprint(path);
        fail(std::string(label) + ": malformed footprint was accepted");
    } catch (const pkgbuild::Error& error) {
        require(error.code() == pkgbuild::ErrorCode::invalid_footprint,
                "malformed footprint produced the wrong error");
    }
}

} // namespace

int main()
{
    TemporaryDirectory temporary;
    const auto manifest = temporary.path() / ".footprint";
    const auto sibling = temporary.path() / "sentinel";
    {
        std::ofstream output(sibling);
        output << "survive\n";
    }

    const auto expected = sample();
    pkgbuild::write_footprint(manifest, expected);
    require(std::filesystem::exists(manifest), "footprint was not written");
    require(std::filesystem::exists(sibling), "atomic write removed sibling");

    const auto loaded = pkgbuild::read_footprint(manifest);
    require(pkgbuild::compare_footprints(expected, loaded).empty(),
            "footprint did not round-trip");

    const auto text = pkgbuild::serialize_footprint(loaded);
    require(text.find("drwxr-xr-x") != std::string::npos,
            "directory mode was not serialized");
    require(text.find("-rwsr-xr-x") != std::string::npos,
            "setuid mode was not serialized");
    require(text.find("usr/\n") != std::string::npos,
            "directory suffix was not serialized");
    require(text.find("usr/bin/link -> tool with space") != std::string::npos,
            "symbolic link target was not serialized");

    auto actual = loaded;
    actual.entries.erase(actual.entries.begin());
    actual.entries[0].mode = 0600;
    actual.entries.push_back(
        {"var/new", pkgbuild::StagedEntryType::regular_file,
         0644, 0, 0, std::nullopt});
    const auto difference = pkgbuild::compare_footprints(loaded, actual);
    require(difference.removed.size() == 1,
            "removed footprint entry was not reported");
    require(difference.changed.size() == 1,
            "changed footprint entry was not reported");
    require(difference.added.size() == 1,
            "added footprint entry was not reported");


    auto unrepresentable = sample();
    unrepresentable.entries[0].path = "bad -> path";
    try {
        (void)pkgbuild::serialize_footprint(unrepresentable);
        fail("unrepresentable path was serialized");
    } catch (const pkgbuild::Error& error) {
        require(error.code() == pkgbuild::ErrorCode::invalid_footprint,
                "unrepresentable path produced the wrong error");
    }

    const auto duplicate = temporary.path() / "duplicate";
    {
        std::ofstream output(duplicate);
        output << "-rw-r--r--  0/0  usr/file\n"
               << "-rw-r--r--  0/0  usr/file\n";
    }
    expect_error(duplicate, "duplicate");

    const auto traversal = temporary.path() / "traversal";
    {
        std::ofstream output(traversal);
        output << "-rw-r--r--  0/0  ../escape\n";
    }
    expect_error(traversal, "traversal");

    const auto bad_mode = temporary.path() / "bad-mode";
    {
        std::ofstream output(bad_mode);
        output << "-rw-r-q-r--  0/0  usr/file\n";
    }
    expect_error(bad_mode, "bad mode");

    const auto bad_directory = temporary.path() / "bad-directory";
    {
        std::ofstream output(bad_directory);
        output << "drwxr-xr-x  0/0  usr\n";
    }
    expect_error(bad_directory, "bad directory");

    std::cout << "footprint model: PASS\n";
    return 0;
}
