#include <pkgbuild/backends/libarchive.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <vector>
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

class FileDescriptor final {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor()
    {
        if (value_ >= 0)
            (void)close(value_);
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_)
    {
        other.value_ = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept
    {
        if (this != &other) {
            if (value_ >= 0)
                (void)close(value_);
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }

    int get() const noexcept { return value_; }

private:
    int value_;
};

using ArchivePtr = std::unique_ptr<archive, ArchiveDeleter>;
using EntryPtr = std::unique_ptr<archive_entry, EntryDeleter>;

[[noreturn]] void archive_error(ErrorCode code,
                                archive* handle,
                                const std::string& operation);

struct DeferredHardlink {
    EntryPtr entry;
    std::filesystem::path path;
    std::filesystem::path target;
};

bool archive_object_exists(const std::filesystem::path& path)
{
    struct stat status {};
    if (lstat(path.c_str(), &status) == 0)
        return true;
    if (errno == ENOENT || errno == ENOTDIR)
        return false;
    throw Error(ErrorCode::extraction_failed,
                "cannot inspect deferred hard-link target '" +
                    path.string() + "': " + std::strerror(errno));
}

void write_deferred_hardlinks(
    archive* output,
    const std::filesystem::path& destination,
    std::vector<DeferredHardlink> deferred)
{
    while (!deferred.empty()) {
        bool progress = false;
        std::vector<DeferredHardlink> pending;
        pending.reserve(deferred.size());

        for (auto& hardlink : deferred) {
            const auto target =
                std::filesystem::absolute(destination / hardlink.target);
            if (!archive_object_exists(target)) {
                pending.push_back(std::move(hardlink));
                continue;
            }

            const auto path =
                std::filesystem::absolute(destination / hardlink.path);
            archive_entry_set_pathname(hardlink.entry.get(), path.c_str());
            archive_entry_set_hardlink(hardlink.entry.get(), target.c_str());
            archive_entry_set_size(hardlink.entry.get(), 0);

            const int header = archive_write_header(output, hardlink.entry.get());
            if (header < ARCHIVE_WARN)
                archive_error(ErrorCode::extraction_failed, output,
                              "creating deferred hard-link entry");
            if (archive_write_finish_entry(output) != ARCHIVE_OK)
                archive_error(ErrorCode::extraction_failed, output,
                              "finishing deferred hard-link entry");
            progress = true;
        }

        if (!progress) {
            const auto& hardlink = pending.front();
            throw Error(
                ErrorCode::extraction_failed,
                "hard-link target '" + hardlink.target.generic_string() +
                    "' for '" + hardlink.path.generic_string() +
                    "' does not exist in source archive");
        }
        deferred = std::move(pending);
    }
}

[[noreturn]] void archive_error(ErrorCode code,
                                archive* handle,
                                const std::string& operation)
{
    const char* message = archive_error_string(handle);
    throw Error(code, operation + ": " + (message ? message : "unknown error"));
}

[[noreturn]] void filesystem_error(const std::string& operation,
                                   const std::filesystem::path& path)
{
    throw Error(ErrorCode::archive_failed,
                operation + " '" + path.string() + "': " +
                    std::strerror(errno));
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

FileDescriptor open_root(const std::filesystem::path& root)
{
    const int descriptor = open(root.c_str(), O_RDONLY | O_DIRECTORY |
                                               O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        filesystem_error("cannot open package root", root);
    return FileDescriptor(descriptor);
}

FileDescriptor duplicate_descriptor(int descriptor)
{
    const int duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0)
        throw Error(ErrorCode::archive_failed,
                    "cannot duplicate package root descriptor: " +
                        std::string(std::strerror(errno)));
    return FileDescriptor(duplicate);
}

FileDescriptor open_regular_beneath(int root,
                                    const std::filesystem::path& relative)
{
    FileDescriptor directory = duplicate_descriptor(root);
    auto component = relative.begin();
    const auto end = relative.end();
    if (component == end)
        throw Error(ErrorCode::archive_failed,
                    "empty package payload path");

    for (;;) {
        const auto current = component++;
        const bool last = component == end;
        const int flags = last ? O_RDONLY | O_CLOEXEC | O_NOFOLLOW
                               : O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
        const int opened = openat(directory.get(), current->c_str(), flags);
        if (opened < 0)
            filesystem_error("cannot open staged package object", relative);
        FileDescriptor next(opened);
        if (last)
            return next;
        directory = std::move(next);
    }
}

void configure_entry(archive_entry* output, const StagedEntry& entry)
{
    const auto pathname = entry.path.generic_string();
    archive_entry_set_pathname(output, pathname.c_str());
    archive_entry_set_perm(output, static_cast<mode_t>(entry.mode));
    archive_entry_set_uid(output, static_cast<la_int64_t>(entry.uid));
    archive_entry_set_gid(output, static_cast<la_int64_t>(entry.gid));
    archive_entry_set_mtime(output,
                            static_cast<time_t>(entry.modification_time.seconds),
                            static_cast<long>(entry.modification_time.nanoseconds));

    switch (entry.type) {
    case StagedEntryType::regular_file:
        archive_entry_set_filetype(output, AE_IFREG);
        if (entry.hardlink_target) {
            const auto target = entry.hardlink_target->generic_string();
            archive_entry_set_hardlink(output, target.c_str());
            archive_entry_set_size(output, 0);
        } else {
            archive_entry_set_size(output, static_cast<la_int64_t>(entry.size));
        }
        break;
    case StagedEntryType::directory:
        archive_entry_set_filetype(output, AE_IFDIR);
        archive_entry_set_size(output, 0);
        break;
    case StagedEntryType::symbolic_link:
        archive_entry_set_filetype(output, AE_IFLNK);
        archive_entry_set_symlink(output, entry.symlink_target->c_str());
        archive_entry_set_size(output, 0);
        break;
    case StagedEntryType::fifo:
        archive_entry_set_filetype(output, AE_IFIFO);
        archive_entry_set_size(output, 0);
        break;
    case StagedEntryType::character_device:
        archive_entry_set_filetype(output, AE_IFCHR);
        archive_entry_set_rdevmajor(output,
                                    static_cast<la_int64_t>(entry.device->major));
        archive_entry_set_rdevminor(output,
                                    static_cast<la_int64_t>(entry.device->minor));
        archive_entry_set_size(output, 0);
        break;
    case StagedEntryType::block_device:
        archive_entry_set_filetype(output, AE_IFBLK);
        archive_entry_set_rdevmajor(output,
                                    static_cast<la_int64_t>(entry.device->major));
        archive_entry_set_rdevminor(output,
                                    static_cast<la_int64_t>(entry.device->minor));
        archive_entry_set_size(output, 0);
        break;
    }
}

bool same_timestamp(const struct stat& status, const StagedEntry& entry)
{
    return static_cast<std::int64_t>(status.st_mtim.tv_sec) ==
               entry.modification_time.seconds &&
           static_cast<std::uint32_t>(status.st_mtim.tv_nsec) ==
               entry.modification_time.nanoseconds;
}

void write_regular_file(archive* output,
                        int root,
                        const StagedEntry& entry)
{
    auto input = open_regular_beneath(root, entry.path);
    struct stat before {};
    if (fstat(input.get(), &before) != 0)
        filesystem_error("cannot inspect staged package payload", entry.path);
    if (!S_ISREG(before.st_mode) ||
        static_cast<std::uint64_t>(before.st_size) != entry.size ||
        !same_timestamp(before, entry))
        throw Error(ErrorCode::archive_failed,
                    "staged package payload changed after metadata capture: " +
                        entry.path.string());

    std::uint64_t total = 0;
    char buffer[64 * 1024];
    for (;;) {
        const ssize_t count = read(input.get(), buffer, sizeof(buffer));
        if (count > 0) {
            total += static_cast<std::uint64_t>(count);
            if (total > entry.size)
                throw Error(ErrorCode::archive_failed,
                            "staged package payload grew during archive creation: " +
                                entry.path.string());

            std::size_t offset = 0;
            while (offset != static_cast<std::size_t>(count)) {
                const auto written = archive_write_data(
                    output, buffer + offset,
                    static_cast<std::size_t>(count) - offset);
                if (written < 0)
                    archive_error(ErrorCode::archive_failed, output,
                                  "writing package file");
                if (written == 0)
                    throw Error(ErrorCode::archive_failed,
                                "package writer made no payload progress");
                offset += static_cast<std::size_t>(written);
            }
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        filesystem_error("cannot read staged package payload", entry.path);
    }

    struct stat after {};
    if (fstat(input.get(), &after) != 0)
        filesystem_error("cannot recheck staged package payload", entry.path);
    if (total != entry.size || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
        throw Error(ErrorCode::archive_failed,
                    "staged package payload changed during archive creation: " +
                        entry.path.string());
}

} // namespace

void LibarchiveBackend::extract(const ExtractRequest& request,
                                EventSink& events) const
{
    emit(events, EventKind::info,
         "Extracting verified source '" + request.source.path().string() +
             "' with libarchive");

    std::filesystem::create_directories(request.destination);

    FileDescriptor source(request.source.duplicate_descriptor());
    if (lseek(source.get(), 0, SEEK_SET) < 0)
        filesystem_error("cannot rewind verified source", request.source.path());

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

    if (archive_read_open_fd(input.get(), source.get(), 64 * 1024) !=
        ARCHIVE_OK)
        archive_error(ErrorCode::extraction_failed, input.get(),
                      "opening verified source archive");

    std::vector<DeferredHardlink> deferred_hardlinks;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(input.get(), &entry) == ARCHIVE_OK) {
        const std::filesystem::path original = archive_entry_pathname(entry);
        if (!safe_archive_path(original))
            throw Error(ErrorCode::extraction_failed,
                        "unsafe path in source archive: " + original.string());

        if (const char* hardlink_name = archive_entry_hardlink(entry)) {
            const std::filesystem::path target = hardlink_name;
            if (!safe_archive_path(target))
                throw Error(ErrorCode::extraction_failed,
                            "unsafe hard-link target in source archive: " +
                                target.string());
            EntryPtr clone(archive_entry_clone(entry));
            if (!clone)
                throw Error(ErrorCode::extraction_failed,
                            "cannot retain deferred hard-link entry");
            deferred_hardlinks.push_back(
                DeferredHardlink{std::move(clone), original, target});
            if (archive_read_data_skip(input.get()) != ARCHIVE_OK)
                archive_error(ErrorCode::extraction_failed, input.get(),
                              "skipping deferred hard-link data");
            continue;
        }

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

    write_deferred_hardlinks(output.get(), request.destination,
                             std::move(deferred_hardlinks));
}

bool LibarchiveBackend::supports(const ArchiveSpec&) const noexcept
{
    return true;
}

ArchiveReceipt LibarchiveBackend::write(const PackageWriteRequest& request,
                                        EventSink& events) const
{
    validate_staged_package(request.package);

    emit(events, EventKind::info,
         "Creating package '" + request.output.string() + "' with libarchive");

    std::filesystem::create_directories(request.output.parent_path());

    ArchivePtr output(archive_write_new());
    if (!output)
        throw Error(ErrorCode::archive_failed,
                    "cannot allocate libarchive output handle");

    configure_format(output.get(), request.archive.format);
    configure_filter(output.get(), request.archive.compression);
    archive_write_set_bytes_per_block(output.get(), 0);

    if (archive_write_open_filename(output.get(), request.output.c_str()) !=
        ARCHIVE_OK)
        archive_error(ErrorCode::archive_failed, output.get(),
                      "opening package output");

    auto root = open_root(request.package.root);
    for (const auto& staged : request.package.entries) {
        EntryPtr entry(archive_entry_new());
        if (!entry)
            throw Error(ErrorCode::archive_failed,
                        "cannot allocate package archive entry");
        configure_entry(entry.get(), staged);

        const int status = archive_write_header(output.get(), entry.get());
        if (status < ARCHIVE_WARN)
            archive_error(ErrorCode::archive_failed, output.get(),
                          "writing package entry header");
        if (status >= ARCHIVE_WARN &&
            staged.type == StagedEntryType::regular_file &&
            !staged.hardlink_target)
            write_regular_file(output.get(), root.get(), staged);

        if (archive_write_finish_entry(output.get()) != ARCHIVE_OK)
            archive_error(ErrorCode::archive_failed, output.get(),
                          "finishing package entry");
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
