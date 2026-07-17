#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <zlib.h>

namespace pkgbuild {
namespace {

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

    int get() const noexcept { return value_; }

private:
    int value_;
};

[[noreturn]] void filesystem_error(const std::string& operation,
                                   const std::filesystem::path& path)
{
    throw Error(ErrorCode::transformation_failed,
                operation + " '" + path.string() + "': " +
                    std::strerror(errno));
}

bool ends_with(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_manpage_path(const std::filesystem::path& path)
{
    std::vector<std::string> components;
    for (const auto& component : path)
        components.push_back(component.string());

    if (components.size() < 3 || ends_with(components.back(), ".gz"))
        return false;

    for (std::size_t index = 0; index + 2 < components.size(); ++index) {
        if (components[index] == "man" &&
            components[index + 1].rfind("man", 0) == 0 &&
            components[index + 1].size() > 3)
            return true;
    }
    return false;
}

std::filesystem::path compressed_path(const std::filesystem::path& path)
{
    return std::filesystem::path(path.generic_string() + ".gz");
}

bool path_exists(const std::filesystem::path& path)
{
    return std::filesystem::symlink_status(path).type() !=
           std::filesystem::file_type::not_found;
}

struct FileIdentity {
    dev_t device{};
    ino_t inode{};
};

FileIdentity regular_identity(const std::filesystem::path& path)
{
    struct stat status {};
    if (lstat(path.c_str(), &status) != 0)
        filesystem_error("cannot inspect staged file", path);
    if (!S_ISREG(status.st_mode))
        throw Error(ErrorCode::transformation_failed,
                    "staged regular entry is not a regular file: " +
                        path.string());
    return {status.st_dev, status.st_ino};
}

void write_all(int descriptor, const unsigned char* data, std::size_t size,
               const std::filesystem::path& path)
{
    std::size_t offset = 0;
    while (offset != size) {
        const ssize_t count = write(descriptor, data + offset, size - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        filesystem_error("cannot write compressed man page", path);
    }
}

std::uintmax_t gzip_file(const std::filesystem::path& input_path,
                         const std::filesystem::path& output_path,
                         const StagedEntry& metadata)
{
    FileDescriptor input(open(input_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() < 0)
        filesystem_error("cannot open man page", input_path);

    struct stat input_status {};
    if (fstat(input.get(), &input_status) != 0)
        filesystem_error("cannot inspect man page", input_path);
    if (!S_ISREG(input_status.st_mode))
        throw Error(ErrorCode::transformation_failed,
                    "man page is not a regular file: " + input_path.string());

    FileDescriptor output(open(output_path.c_str(),
                               O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                   O_NOFOLLOW,
                               0600));
    if (output.get() < 0)
        filesystem_error("cannot create compressed man page", output_path);

    bool success = false;
    try {
        z_stream stream {};
        if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                         Z_DEFAULT_STRATEGY) != Z_OK)
            throw Error(ErrorCode::transformation_failed,
                        "cannot initialize gzip compressor");

        gz_header header {};
        header.time = 0;
        header.os = 3;
        if (deflateSetHeader(&stream, &header) != Z_OK) {
            (void)deflateEnd(&stream);
            throw Error(ErrorCode::transformation_failed,
                        "cannot configure gzip header");
        }

        unsigned char input_buffer[64 * 1024];
        unsigned char output_buffer[64 * 1024];
        int flush = Z_NO_FLUSH;
        int result = Z_OK;
        do {
            ssize_t count = 0;
            do {
                count = read(input.get(), input_buffer, sizeof(input_buffer));
            } while (count < 0 && errno == EINTR);
            if (count < 0) {
                (void)deflateEnd(&stream);
                filesystem_error("cannot read man page", input_path);
            }

            flush = count == 0 ? Z_FINISH : Z_NO_FLUSH;
            stream.next_in = input_buffer;
            stream.avail_in = static_cast<uInt>(count);
            do {
                stream.next_out = output_buffer;
                stream.avail_out = sizeof(output_buffer);
                result = deflate(&stream, flush);
                if (result == Z_STREAM_ERROR) {
                    (void)deflateEnd(&stream);
                    throw Error(ErrorCode::transformation_failed,
                                "gzip compression failed");
                }
                const std::size_t produced =
                    sizeof(output_buffer) - stream.avail_out;
                write_all(output.get(), output_buffer, produced, output_path);
            } while (stream.avail_out == 0);
        } while (flush != Z_FINISH || result != Z_STREAM_END);

        if (deflateEnd(&stream) != Z_OK)
            throw Error(ErrorCode::transformation_failed,
                        "cannot finish gzip compression");

        if (fchmod(output.get(), input_status.st_mode & 0777) != 0)
            filesystem_error("cannot set compressed man page mode", output_path);

        const timespec times[2] = {
            {metadata.modification_time.seconds,
             static_cast<long>(metadata.modification_time.nanoseconds)},
            {metadata.modification_time.seconds,
             static_cast<long>(metadata.modification_time.nanoseconds)},
        };
        if (futimens(output.get(), times) != 0)
            filesystem_error("cannot set compressed man page time", output_path);

        struct stat output_status {};
        if (fstat(output.get(), &output_status) != 0)
            filesystem_error("cannot inspect compressed man page", output_path);
        success = true;
        return static_cast<std::uintmax_t>(output_status.st_size);
    } catch (...) {
        if (!success)
            (void)unlink(output_path.c_str());
        throw;
    }
}

using GroupMap = std::map<std::filesystem::path, std::vector<std::size_t>>;

GroupMap regular_groups(const StagedPackage& package)
{
    GroupMap groups;
    for (std::size_t index = 0; index != package.entries.size(); ++index) {
        const auto& entry = package.entries[index];
        if (entry.type != StagedEntryType::regular_file)
            continue;
        groups[entry.hardlink_target.value_or(entry.path)].push_back(index);
    }
    return groups;
}

bool canonical_package_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path != path.lexically_normal())
        return false;
    for (const auto& component : path) {
        if (component == "." || component == ".." || component.empty())
            return false;
    }
    return true;
}

std::optional<std::filesystem::path>
resolved_symlink_target(const StagedEntry& entry)
{
    if (!entry.symlink_target)
        return std::nullopt;
    const std::filesystem::path target(*entry.symlink_target);
    auto resolved = target.is_absolute()
        ? target.relative_path().lexically_normal()
        : (entry.path.parent_path() / target).lexically_normal();
    if (!canonical_package_path(resolved))
        return std::nullopt;
    return resolved;
}

void sort_entries(StagedPackage& package)
{
    std::sort(package.entries.begin(), package.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.path.generic_string() < right.path.generic_string();
              });
}

void compress_manpages(StagedPackage& package,
                       TransformationReceipt& receipt,
                       EventSink& events)
{
    const auto groups = regular_groups(package);
    std::map<std::filesystem::path, std::filesystem::path> rewritten;

    for (const auto& [canonical, indices] : groups) {
        bool any_manpage = false;
        bool all_manpages = true;
        for (const auto index : indices) {
            const bool manpage = is_manpage_path(package.entries[index].path);
            any_manpage = any_manpage || manpage;
            all_manpages = all_manpages && manpage;
        }
        if (!any_manpage)
            continue;
        if (!all_manpages) {
            emit(events, EventKind::warning,
                 "Not compressing mixed hardlink group containing '" +
                     canonical.string() + "'");
            continue;
        }

        const auto& primary = package.entries[indices.front()];
        const auto input_size = primary.size;
        const auto input = package.root / primary.path;
        const auto output_relative = compressed_path(primary.path);
        const auto output = package.root / output_relative;

        std::vector<std::filesystem::path> inputs;
        std::vector<std::filesystem::path> outputs;
        inputs.reserve(indices.size());
        outputs.reserve(indices.size());

        const auto identity = regular_identity(input);
        for (const auto index : indices) {
            const auto& entry = package.entries[index];
            const auto full = package.root / entry.path;
            const auto current = regular_identity(full);
            if (current.device != identity.device || current.inode != identity.inode)
                throw Error(ErrorCode::transformation_failed,
                            "staged hardlink group changed before compression: " +
                                entry.path.string());
            const auto compressed = compressed_path(entry.path);
            if (path_exists(package.root / compressed))
                throw Error(ErrorCode::transformation_failed,
                            "compressed man page already exists: " +
                                compressed.string());
            inputs.push_back(entry.path);
            outputs.push_back(compressed);
        }

        emit(events, EventKind::info,
             "Compressing man page '" + primary.path.string() + "'");
        const auto output_size = gzip_file(input, output, primary);

        std::vector<std::filesystem::path> created{output};
        try {
            for (std::size_t item = 1; item != outputs.size(); ++item) {
                const auto alias = package.root / outputs[item];
                if (link(output.c_str(), alias.c_str()) != 0)
                    filesystem_error("cannot create compressed man page hardlink",
                                     alias);
                created.push_back(alias);
            }
        } catch (...) {
            for (auto iterator = created.rbegin(); iterator != created.rend();
                 ++iterator)
                (void)unlink(iterator->c_str());
            throw;
        }

        for (const auto& path : inputs) {
            if (unlink((package.root / path).c_str()) != 0)
                filesystem_error("cannot remove uncompressed man page",
                                 package.root / path);
        }

        for (std::size_t item = 0; item != indices.size(); ++item) {
            auto& entry = package.entries[indices[item]];
            rewritten.emplace(entry.path, outputs[item]);
            entry.path = outputs[item];
            entry.size = output_size;
            entry.hardlink_target = item == 0
                ? std::nullopt
                : std::optional<std::filesystem::path>(outputs.front());
        }

        receipt.changes.push_back(TransformationChange{
            TransformationKind::compress_manpage,
            std::move(inputs),
            std::move(outputs),
            input_size,
            output_size,
        });
    }

    for (auto& entry : package.entries) {
        if (entry.type != StagedEntryType::symbolic_link ||
            !is_manpage_path(entry.path))
            continue;

        const auto target = resolved_symlink_target(entry);
        if (!target)
            continue;
        const auto replacement = rewritten.find(*target);
        if (replacement == rewritten.end())
            continue;

        const auto old_path = entry.path;
        const auto new_path = compressed_path(old_path);
        const auto old_full = package.root / old_path;
        const auto new_full = package.root / new_path;
        if (path_exists(new_full))
            throw Error(ErrorCode::transformation_failed,
                        "compressed man page symlink already exists: " +
                            new_path.string());

        std::string new_target = *entry.symlink_target + ".gz";
        if (symlink(new_target.c_str(), new_full.c_str()) != 0)
            filesystem_error("cannot create compressed man page symlink", new_full);
        if (unlink(old_full.c_str()) != 0) {
            const int saved = errno;
            (void)unlink(new_full.c_str());
            errno = saved;
            filesystem_error("cannot remove old man page symlink", old_full);
        }

        entry.path = new_path;
        entry.symlink_target = new_target;
        receipt.changes.push_back(TransformationChange{
            TransformationKind::rewrite_manpage_symlink,
            {old_path},
            {new_path},
            0,
            0,
        });
    }

    sort_entries(package);
}

} // namespace

TransformationReceipt PackageTreeTransformer::transform(
    const PackageTransformRequest& request,
    EventSink& events) const
{
    validate_staged_package(request.package);
    TransformationReceipt receipt{std::string(name()), {}};
    if (request.policy.compress_manpages)
        compress_manpages(request.package, receipt, events);
    validate_staged_package(request.package);
    return receipt;
}

} // namespace pkgbuild
