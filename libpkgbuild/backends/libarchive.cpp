#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/error.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace pkgbuild {
namespace {

struct ArchiveDeleter {
    void operator()(archive* value) const noexcept
    {
        if (value)
            archive_free(value);
    }
};

struct EntryDeleter {
    void operator()(archive_entry* value) const noexcept
    {
        if (value)
            archive_entry_free(value);
    }
};

using ArchivePtr = std::unique_ptr<archive, ArchiveDeleter>;
using EntryPtr = std::unique_ptr<archive_entry, EntryDeleter>;

[[noreturn]] void archive_error(ErrorCode code,
                                archive* handle,
                                const std::string& operation)
{
    const char* message = archive_error_string(handle);
    throw Error(code, operation + ": " + (message ? message : "unknown error"));
}

void copy_archive_data(archive* input, archive* output, ErrorCode code)
{
    const void* buffer = nullptr;
    size_t size = 0;
    la_int64_t offset = 0;

    for (;;) {
        const int status = archive_read_data_block(input, &buffer, &size, &offset);
        if (status == ARCHIVE_EOF)
            return;
        if (status != ARCHIVE_OK)
            archive_error(code, input, "reading archive data");
        if (archive_write_data_block(output, buffer, size, offset) != ARCHIVE_OK)
            archive_error(code, output, "writing archive data");
    }
}

void configure_format(archive* output, ArchiveFormat format)
{
    int status = ARCHIVE_FATAL;
    switch (format) {
    case ArchiveFormat::gnutar:
        status = archive_write_set_format_gnutar(output);
        break;
    case ArchiveFormat::pax:
        status = archive_write_set_format_pax_restricted(output);
        break;
    case ArchiveFormat::ustar:
        status = archive_write_set_format_ustar(output);
        break;
    case ArchiveFormat::v7:
        status = archive_write_set_format_v7tar(output);
        break;
    }
    if (status != ARCHIVE_OK)
        archive_error(ErrorCode::archive_failed, output,
                      "configuring archive format");
}

void configure_filter(archive* output, Compression compression)
{
    int status = ARCHIVE_FATAL;
    switch (compression) {
    case Compression::gzip:
        status = archive_write_add_filter_gzip(output);
        break;
    case Compression::bzip2:
        status = archive_write_add_filter_bzip2(output);
        break;
    case Compression::xz:
        status = archive_write_add_filter_xz(output);
        break;
    case Compression::lzip:
        status = archive_write_add_filter_lzip(output);
        break;
    case Compression::zstd:
        status = archive_write_add_filter_zstd(output);
        break;
    }
    if (status != ARCHIVE_OK)
        archive_error(ErrorCode::archive_failed, output,
                      "configuring archive compression");
}

bool safe_archive_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
        return false;
    for (const auto& component : path) {
        if (component == "..")
            return false;
    }
    return true;
}

void write_regular_file(archive* output, const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw Error(ErrorCode::archive_failed,
                    "cannot read package file: " + path.string());

    char buffer[64 * 1024];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const auto count = input.gcount();
        if (count > 0 && archive_write_data(output, buffer,
                                             static_cast<size_t>(count)) < 0)
            archive_error(ErrorCode::archive_failed, output,
                          "writing package file");
    }
}

} // namespace

void LibarchiveBackend::extract(const ExtractRequest& request,
                                EventSink& events) const
{
    emit(events, EventKind::info,
         "Extracting '" + request.archive.string() + "' with libarchive");

    std::filesystem::create_directories(request.destination);

    ArchivePtr input(archive_read_new());
    ArchivePtr output(archive_write_disk_new());
    if (!input || !output)
        throw Error(ErrorCode::extraction_failed,
                    "cannot allocate libarchive handles");

    archive_read_support_filter_all(input.get());
    archive_read_support_format_all(input.get());
    archive_write_disk_set_options(
        output.get(),
        ARCHIVE_EXTRACT_TIME |
        ARCHIVE_EXTRACT_PERM |
        ARCHIVE_EXTRACT_SECURE_SYMLINKS);
    archive_write_disk_set_standard_lookup(output.get());

    if (archive_read_open_filename(input.get(), request.archive.c_str(),
                                   64 * 1024) != ARCHIVE_OK)
        archive_error(ErrorCode::extraction_failed, input.get(),
                      "opening source archive");

    archive_entry* entry = nullptr;
    while (archive_read_next_header(input.get(), &entry) == ARCHIVE_OK) {
        const std::filesystem::path original = archive_entry_pathname(entry);
        if (!safe_archive_path(original))
            throw Error(ErrorCode::extraction_failed,
                        "unsafe path in source archive: " + original.string());

        const auto destination =
            std::filesystem::absolute(request.destination / original);
        archive_entry_set_pathname(entry, destination.c_str());

        const int header = archive_write_header(output.get(), entry);
        if (header < ARCHIVE_WARN)
            archive_error(ErrorCode::extraction_failed, output.get(),
                          "creating extracted entry");
        if (header == ARCHIVE_OK && archive_entry_size(entry) > 0)
            copy_archive_data(input.get(), output.get(),
                              ErrorCode::extraction_failed);

        if (archive_write_finish_entry(output.get()) != ARCHIVE_OK)
            archive_error(ErrorCode::extraction_failed, output.get(),
                          "finishing extracted entry");
    }

    if (archive_errno(input.get()) != 0)
        archive_error(ErrorCode::extraction_failed, input.get(),
                      "reading source archive");
}

bool LibarchiveBackend::supports(const ArchiveSpec&) const noexcept
{
    return true;
}

ArchiveReceipt LibarchiveBackend::write(const PackageWriteRequest& request,
                                        EventSink& events) const
{
    if (!std::filesystem::is_directory(request.root))
        throw Error(ErrorCode::archive_failed,
                    "package root is not a directory: " + request.root.string());

    emit(events, EventKind::info,
         "Creating package '" + request.output.string() + "' with libarchive");

    std::filesystem::create_directories(request.output.parent_path());

    ArchivePtr output(archive_write_new());
    ArchivePtr disk(archive_read_disk_new());
    if (!output || !disk)
        throw Error(ErrorCode::archive_failed,
                    "cannot allocate libarchive handles");

    configure_format(output.get(), request.archive.format);
    configure_filter(output.get(), request.archive.compression);
    archive_write_set_bytes_per_block(output.get(), 0);

    if (archive_write_open_filename(output.get(), request.output.c_str()) != ARCHIVE_OK)
        archive_error(ErrorCode::archive_failed, output.get(),
                      "opening package output");

    archive_read_disk_set_standard_lookup(disk.get());
    archive_read_disk_set_symlink_physical(disk.get());

    const auto root = std::filesystem::absolute(request.root);
    for (std::filesystem::recursive_directory_iterator iterator(root), end;
         iterator != end; ++iterator) {
        const auto path = iterator->path();
        struct stat st {};
        if (lstat(path.c_str(), &st) != 0)
            throw Error(ErrorCode::archive_failed,
                        "lstat failed for " + path.string() + ": " +
                            std::strerror(errno));

        EntryPtr entry(archive_entry_new());
        archive_entry_set_pathname(entry.get(), path.c_str());
        if (archive_read_disk_entry_from_file(disk.get(), entry.get(), -1, &st) !=
            ARCHIVE_OK)
            archive_error(ErrorCode::archive_failed, disk.get(),
                          "reading package entry metadata");

        const auto relative = path.lexically_relative(root).generic_string();
        archive_entry_set_pathname(entry.get(), relative.c_str());

        const int status = archive_write_header(output.get(), entry.get());
        if (status < ARCHIVE_WARN)
            archive_error(ErrorCode::archive_failed, output.get(),
                          "writing package entry header");

        if (status == ARCHIVE_OK && S_ISREG(st.st_mode))
            write_regular_file(output.get(), path);
    }

    if (archive_write_close(output.get()) != ARCHIVE_OK)
        archive_error(ErrorCode::archive_failed, output.get(),
                      "closing package output");

    return ArchiveReceipt{
        std::filesystem::absolute(request.output),
        std::filesystem::file_size(request.output),
        request.archive,
    };
}

} // namespace pkgbuild
