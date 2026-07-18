#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/event.hpp>
#include <pkgbuild/source.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::string pattern = "/tmp/libpkgbuild-extraction.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* created = mkdtemp(storage.data());
    if (!created)
        fail("cannot create temporary directory");
    return created;
}

void require_archive(int status, archive* handle, const std::string& operation)
{
    if (status == ARCHIVE_OK)
        return;
    const char* message = archive_error_string(handle);
    fail(operation + ": " + (message ? message : "unknown archive error"));
}

void write_directory(archive* output, const std::string& path)
{
    archive_entry* entry = archive_entry_new();
    if (!entry)
        fail("cannot allocate directory entry");
    archive_entry_set_pathname(entry, path.c_str());
    archive_entry_set_filetype(entry, AE_IFDIR);
    archive_entry_set_perm(entry, 0755);
    archive_entry_set_size(entry, 0);
    require_archive(archive_write_header(output, entry), output,
                    "writing directory header");
    archive_entry_free(entry);
}

void write_hardlink(archive* output,
                    const std::string& path,
                    const std::string& target)
{
    archive_entry* entry = archive_entry_new();
    if (!entry)
        fail("cannot allocate hard-link entry");
    archive_entry_set_pathname(entry, path.c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_hardlink(entry, target.c_str());
    archive_entry_set_size(entry, 0);
    require_archive(archive_write_header(output, entry), output,
                    "writing hard-link header");
    archive_entry_free(entry);
}

void write_file(archive* output,
                const std::string& path,
                const std::string& payload)
{
    archive_entry* entry = archive_entry_new();
    if (!entry)
        fail("cannot allocate file entry");
    archive_entry_set_pathname(entry, path.c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, static_cast<la_int64_t>(payload.size()));
    require_archive(archive_write_header(output, entry), output,
                    "writing file header");
    require(archive_write_data(output, payload.data(), payload.size()) ==
                static_cast<la_ssize_t>(payload.size()),
            "cannot write file payload");
    archive_entry_free(entry);
}

void make_hardlink_archive(const std::filesystem::path& path,
                           bool include_target)
{
    archive* output = archive_write_new();
    if (!output)
        fail("cannot allocate archive writer");
    require_archive(archive_write_set_format_gnutar(output), output,
                    "selecting archive format");
    require_archive(archive_write_open_filename(output, path.c_str()), output,
                    "opening archive output");

    write_directory(output, "fixture/");
    write_hardlink(output, "fixture/link", "fixture/intermediate");
    write_hardlink(output, "fixture/intermediate", "fixture/target");
    if (include_target)
        write_file(output, "fixture/target", "forward hard link\n");

    require_archive(archive_write_close(output), output, "closing archive");
    require_archive(archive_write_free(output), output, "freeing archive writer");
}

pkgbuild::VerifiedSource open_source(const std::filesystem::path& path)
{
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0)
        fail("cannot open source archive");
    return pkgbuild::VerifiedSource(descriptor, path, {});
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    std::filesystem::path root;
    try {
        root = temporary_directory();
        const auto archive_path = root / "forward-hardlink.tar";
        const auto destination = root / "out";
        make_hardlink_archive(archive_path, true);

        auto source = open_source(archive_path);
        pkgbuild::LibarchiveBackend backend;
        pkgbuild::NullEventSink events;
        backend.extract(pkgbuild::ExtractRequest{source, destination}, events);

        const auto target = destination / "fixture/target";
        const auto intermediate = destination / "fixture/intermediate";
        const auto link = destination / "fixture/link";
        require(read_file(target) == "forward hard link\n",
                "target payload was not extracted");
        require(read_file(intermediate) == "forward hard link\n",
                "intermediate hard-link payload differs");
        require(read_file(link) == "forward hard link\n",
                "forward hard-link payload differs");

        struct stat target_status {};
        struct stat intermediate_status {};
        struct stat link_status {};
        require(stat(target.c_str(), &target_status) == 0,
                "cannot stat extracted target");
        require(stat(intermediate.c_str(), &intermediate_status) == 0,
                "cannot stat intermediate hard link");
        require(stat(link.c_str(), &link_status) == 0,
                "cannot stat extracted hard link");
        require(target_status.st_dev == intermediate_status.st_dev &&
                    target_status.st_ino == intermediate_status.st_ino &&
                    target_status.st_dev == link_status.st_dev &&
                    target_status.st_ino == link_status.st_ino,
                "forward hard-link chain was copied instead of linked");

        const auto unresolved_archive = root / "unresolved-hardlink.tar";
        make_hardlink_archive(unresolved_archive, false);
        auto unresolved_source = open_source(unresolved_archive);
        try {
            backend.extract(
                pkgbuild::ExtractRequest{unresolved_source, root / "unresolved"},
                events);
            fail("unresolved hard-link target was accepted");
        } catch (const pkgbuild::Error& error) {
            require(error.code() == pkgbuild::ErrorCode::extraction_failed,
                    "unexpected error for unresolved hard-link target");
        }

        std::filesystem::remove_all(root);
        std::cout << "source extraction: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "source extraction: " << error.what() << '\n';
        return 1;
    }
}
