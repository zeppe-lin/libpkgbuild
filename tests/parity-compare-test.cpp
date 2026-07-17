#include "../tools/parity/archive_compare.hpp"

#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/event.hpp>
#include <pkgbuild/types.hpp>

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <array>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/pkgbuild-parity-compare.XXXXXX";
        std::vector<char> storage(pattern.begin(), pattern.end());
        storage.push_back('\0');
        char* created = mkdtemp(storage.data());
        if (created == nullptr)
            throw std::runtime_error("cannot create temporary directory");
        path_ = created;
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

pkgbuild::StagedEntry regular(const std::string& path,
                              const std::filesystem::path& host_path,
                              std::uint32_t mode)
{
    struct stat status {};
    if (lstat(host_path.c_str(), &status) != 0)
        throw std::runtime_error("cannot inspect staged test payload");

    pkgbuild::StagedEntry entry;
    entry.path = path;
    entry.type = pkgbuild::StagedEntryType::regular_file;
    entry.mode = mode;
    entry.uid = 12;
    entry.gid = 34;
    entry.size = static_cast<std::uint64_t>(status.st_size);
    entry.modification_time.seconds = status.st_mtim.tv_sec;
    entry.modification_time.nanoseconds =
        static_cast<std::uint32_t>(status.st_mtim.tv_nsec);
    return entry;
}

pkgbuild::StagedEntry hardlink(const std::string& path,
                               const std::string& target,
                               std::uint32_t mode)
{
    pkgbuild::StagedEntry entry;
    entry.path = path;
    entry.type = pkgbuild::StagedEntryType::regular_file;
    entry.mode = mode;
    entry.uid = 12;
    entry.gid = 34;
    entry.hardlink_target = target;
    return entry;
}

std::filesystem::path write_hardlink_archive(
    const std::filesystem::path& directory,
    const std::string& filename,
    bool reverse,
    std::uint32_t mode,
    const std::string& payload)
{
    const auto root = directory / (filename + ".root");
    std::filesystem::create_directories(root / "usr/share/test");
    const auto a = root / "usr/share/test/a";
    const auto b = root / "usr/share/test/b";
    const auto primary = reverse ? b : a;
    const auto alias = reverse ? a : b;
    {
        std::ofstream output(primary, std::ios::binary);
        output << payload;
    }
    if (chmod(primary.c_str(), static_cast<mode_t>(mode)) != 0)
        throw std::runtime_error("cannot set staged test payload mode");
    std::filesystem::create_hard_link(primary, alias);

    pkgbuild::StagedPackage package;
    package.root = root;
    if (reverse) {
        package.entries.push_back(regular("usr/share/test/b", b, mode));
        package.entries.push_back(hardlink("usr/share/test/a",
                                            "usr/share/test/b", mode));
    } else {
        package.entries.push_back(regular("usr/share/test/a", a, mode));
        package.entries.push_back(hardlink("usr/share/test/b",
                                            "usr/share/test/a", mode));
    }

    pkgbuild::LibarchiveBackend writer;
    pkgbuild::NullEventSink events;
    const auto output = directory / filename;
    writer.write(pkgbuild::PackageWriteRequest{
                     std::move(package), output,
                     pkgbuild::ArchiveSpec{}},
                 events);
    return output;
}


std::filesystem::path write_reverse_hardlink_archive(
    const std::filesystem::path& directory,
    const std::string& filename,
    std::uint32_t mode,
    const std::string& payload)
{
    const auto output = directory / filename;
    archive* writer = archive_write_new();
    if (writer == nullptr)
        throw std::runtime_error("cannot allocate reverse archive writer");
    archive_write_set_format_gnutar(writer);
    archive_write_add_filter_gzip(writer);
    if (archive_write_open_filename(writer, output.c_str()) != ARCHIVE_OK) {
        archive_write_free(writer);
        throw std::runtime_error("cannot open reverse archive output");
    }

    archive_entry* primary = archive_entry_new();
    archive_entry_set_pathname(primary, "usr/share/test/b");
    archive_entry_set_filetype(primary, AE_IFREG);
    archive_entry_set_perm(primary, static_cast<mode_t>(mode));
    archive_entry_set_uid(primary, 12);
    archive_entry_set_gid(primary, 34);
    archive_entry_set_size(primary, static_cast<la_int64_t>(payload.size()));
    if (archive_write_header(writer, primary) != ARCHIVE_OK ||
        archive_write_data(writer, payload.data(), payload.size()) < 0) {
        archive_entry_free(primary);
        archive_write_free(writer);
        throw std::runtime_error("cannot write reverse archive payload");
    }
    archive_entry_free(primary);

    archive_entry* alias = archive_entry_new();
    archive_entry_set_pathname(alias, "usr/share/test/a");
    archive_entry_set_filetype(alias, AE_IFREG);
    archive_entry_set_perm(alias, static_cast<mode_t>(mode));
    archive_entry_set_uid(alias, 12);
    archive_entry_set_gid(alias, 34);
    archive_entry_set_hardlink(alias, "usr/share/test/b");
    archive_entry_set_size(alias, 0);
    if (archive_write_header(writer, alias) != ARCHIVE_OK) {
        archive_entry_free(alias);
        archive_write_free(writer);
        throw std::runtime_error("cannot write reverse archive hardlink");
    }
    archive_entry_free(alias);

    if (archive_write_close(writer) != ARCHIVE_OK ||
        archive_write_free(writer) != ARCHIVE_OK)
        throw std::runtime_error("cannot close reverse archive");
    return output;
}

std::string gzip_payload(const std::string& payload,
                         const std::string& original_name,
                         std::uint32_t modification_time)
{
    z_stream stream {};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
        throw std::runtime_error("cannot initialize test gzip compressor");

    std::vector<unsigned char> name(original_name.begin(), original_name.end());
    name.push_back('\0');
    gz_header header {};
    header.time = modification_time;
    header.os = 3;
    if (!original_name.empty())
        header.name = name.data();
    if (deflateSetHeader(&stream, &header) != Z_OK) {
        (void)deflateEnd(&stream);
        throw std::runtime_error("cannot configure test gzip header");
    }

    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<char*>(payload.data()));
    stream.avail_in = static_cast<uInt>(payload.size());

    std::string output;
    std::array<unsigned char, 256> buffer {};
    for (;;) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        const int result = deflate(&stream, Z_FINISH);
        output.append(reinterpret_cast<const char*>(buffer.data()),
                      buffer.size() - stream.avail_out);
        if (result == Z_STREAM_END)
            break;
        if (result != Z_OK) {
            (void)deflateEnd(&stream);
            throw std::runtime_error("cannot create test gzip payload");
        }
    }
    if (deflateEnd(&stream) != Z_OK)
        throw std::runtime_error("cannot finish test gzip payload");
    return output;
}

std::filesystem::path write_single_file_archive(
    const std::filesystem::path& directory,
    const std::string& filename,
    const std::string& package_path,
    const std::string& payload)
{
    const auto root = directory / (filename + ".root");
    const auto host_path = root / package_path;
    std::filesystem::create_directories(host_path.parent_path());
    {
        std::ofstream output(host_path, std::ios::binary);
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    pkgbuild::StagedPackage package;
    package.root = root;
    package.entries.push_back(regular(package_path, host_path, 0644));

    pkgbuild::LibarchiveBackend writer;
    pkgbuild::NullEventSink events;
    const auto output = directory / filename;
    writer.write(pkgbuild::PackageWriteRequest{
                     std::move(package), output,
                     pkgbuild::ArchiveSpec{}},
                 events);
    return output;
}


bool has_field(const pkgbuild::parity::ArchiveComparison& report,
               const std::string& field)
{
    for (const auto& difference : report.differences) {
        if (difference.field == field)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    try {
        TemporaryDirectory temporary;
        const auto reference = write_hardlink_archive(
            temporary.path(), "reference.pkg.tar.gz", false, 0755, "payload\n");
        const auto reversed = write_reverse_hardlink_archive(
            temporary.path(), "reversed.pkg.tar.gz", 0755, "payload\n");
        const auto mode_changed = write_hardlink_archive(
            temporary.path(), "mode.pkg.tar.gz", false, 0644, "payload\n");
        const auto payload_changed = write_hardlink_archive(
            temporary.path(), "payload.pkg.tar.gz", false, 0755, "changed\n");
        const auto man_reference = write_single_file_archive(
            temporary.path(), "man-reference.pkg.tar.gz",
            "usr/share/man/man1/tool.1.gz",
            gzip_payload("manual payload\n", "tool.1", 100));
        const auto man_repacked = write_single_file_archive(
            temporary.path(), "man-repacked.pkg.tar.gz",
            "usr/share/man/man1/tool.1.gz",
            gzip_payload("manual payload\n", "", 0));
        const auto man_changed = write_single_file_archive(
            temporary.path(), "man-changed.pkg.tar.gz",
            "usr/share/man/man1/tool.1.gz",
            gzip_payload("changed payload\n", "tool.1", 100));
        const auto ordinary_gzip_reference = write_single_file_archive(
            temporary.path(), "ordinary-gzip-reference.pkg.tar.gz",
            "usr/share/data/tool.1.gz",
            gzip_payload("manual payload\n", "tool.1", 100));
        const auto ordinary_gzip_repacked = write_single_file_archive(
            temporary.path(), "ordinary-gzip-repacked.pkg.tar.gz",
            "usr/share/data/tool.1.gz",
            gzip_payload("manual payload\n", "", 0));
        const auto invalid_man = write_single_file_archive(
            temporary.path(), "invalid-man.pkg.tar.gz",
            "usr/share/man/man1/tool.1.gz", "not gzip");

        require(pkgbuild::parity::compare_archives(reference, reversed).equivalent(),
                "hardlink target orientation changed semantic comparison");

        const auto mode = pkgbuild::parity::compare_archives(reference,
                                                              mode_changed);
        require(!mode.equivalent() && has_field(mode, "mode"),
                "mode difference was not reported");

        const auto payload = pkgbuild::parity::compare_archives(
            reference, payload_changed);
        require(!payload.equivalent() && has_field(payload, "payload-sha256"),
                "payload difference was not reported");

        require(pkgbuild::parity::compare_archives(man_reference, man_repacked)
                    .equivalent(),
                "gzip man page metadata changed semantic comparison");

        const auto changed_man = pkgbuild::parity::compare_archives(
            man_reference, man_changed);
        require(!changed_man.equivalent() &&
                    has_field(changed_man, "payload-sha256"),
                "changed decompressed man page was not reported");

        const auto raw_gzip = pkgbuild::parity::compare_archives(
            ordinary_gzip_reference, ordinary_gzip_repacked);
        require(!raw_gzip.equivalent() &&
                    has_field(raw_gzip, "payload-sha256"),
                "non-man gzip payload was normalized");

        bool invalid_rejected = false;
        try {
            (void)pkgbuild::parity::compare_archives(man_reference, invalid_man);
        } catch (const std::runtime_error&) {
            invalid_rejected = true;
        }
        require(invalid_rejected, "invalid gzip man page was accepted");

        std::cout << "semantic archive comparison: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "semantic archive comparison: FAIL: " << error.what()
                  << '\n';
        return 1;
    }
}
