#include <pkgbuild/backends/normalize.hpp>
#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <regex.h>
#include <set>
#include <string_view>
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


enum class StripMode {
    none,
    all,
    unneeded,
    debug,
};

std::uint16_t read_u16(const unsigned char* data, bool little_endian)
{
    if (little_endian)
        return static_cast<std::uint16_t>(data[0]) |
               static_cast<std::uint16_t>(data[1] << 8U);
    return static_cast<std::uint16_t>(data[1]) |
           static_cast<std::uint16_t>(data[0] << 8U);
}

std::uint32_t read_u32(const unsigned char* data, bool little_endian)
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index != 4; ++index) {
        const unsigned int source = little_endian ? index : 3U - index;
        value |= static_cast<std::uint32_t>(data[source]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const unsigned char* data, bool little_endian)
{
    std::uint64_t value = 0;
    for (unsigned int index = 0; index != 8; ++index) {
        const unsigned int source = little_endian ? index : 7U - index;
        value |= static_cast<std::uint64_t>(data[source]) << (index * 8U);
    }
    return value;
}

bool has_program_interpreter(int descriptor,
                             const unsigned char* header,
                             std::size_t header_size,
                             bool little_endian)
{
    const bool elf64 = header[4] == 2;
    const std::size_t required = elf64 ? 58 : 46;
    if (header_size < required)
        return false;

    const std::uint64_t offset = elf64
        ? read_u64(header + 32, little_endian)
        : read_u32(header + 28, little_endian);
    const std::uint16_t entry_size = read_u16(
        header + (elf64 ? 54 : 42), little_endian);
    const std::uint16_t count = read_u16(
        header + (elf64 ? 56 : 44), little_endian);
    if (entry_size < 4 || count == 0 || count > 4096)
        return false;

    struct stat status {};
    if (fstat(descriptor, &status) != 0)
        return false;
    const auto file_size = static_cast<std::uint64_t>(status.st_size);
    if (offset > file_size ||
        static_cast<std::uint64_t>(entry_size) * count > file_size - offset)
        return false;

    unsigned char type[4] {};
    for (std::uint16_t index = 0; index != count; ++index) {
        const off_t position = static_cast<off_t>(
            offset + static_cast<std::uint64_t>(entry_size) * index);
        ssize_t read_count = 0;
        do {
            read_count = pread(descriptor, type, sizeof(type), position);
        } while (read_count < 0 && errno == EINTR);
        if (read_count != static_cast<ssize_t>(sizeof(type)))
            return false;
        if (read_u32(type, little_endian) == 3)
            return true;
    }
    return false;
}

StripMode strip_mode(const std::filesystem::path& path)
{
    FileDescriptor input(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() < 0)
        filesystem_error("cannot open strip candidate", path);

    unsigned char header[64] {};
    ssize_t count = 0;
    do {
        count = read(input.get(), header, sizeof(header));
    } while (count < 0 && errno == EINTR);
    if (count < 0)
        filesystem_error("cannot read strip candidate", path);

    if (count >= 18 && header[0] == 0x7f && header[1] == 'E' &&
        header[2] == 'L' && header[3] == 'F' &&
        (header[4] == 1 || header[4] == 2) &&
        (header[5] == 1 || header[5] == 2)) {
        const bool little_endian = header[5] == 1;
        const auto type = read_u16(header + 16, little_endian);
        if (type == 2)
            return StripMode::all;
        if (type == 3)
            return has_program_interpreter(
                       input.get(), header, static_cast<std::size_t>(count),
                       little_endian)
                ? StripMode::all
                : StripMode::unneeded;
    }

    static constexpr std::string_view ar = "!<arch>\n";
    if (count >= 8 && std::equal(ar.begin(), ar.end(), header))
        return StripMode::debug;
    return StripMode::none;
}

const char* strip_option(StripMode mode)
{
    switch (mode) {
    case StripMode::all: return "--strip-all";
    case StripMode::unneeded: return "--strip-unneeded";
    case StripMode::debug: return "--strip-debug";
    case StripMode::none: break;
    }
    return "";
}

class BasicRegex final {
public:
    explicit BasicRegex(const std::string& pattern)
    {
        const int status = regcomp(&value_, pattern.c_str(), REG_NOSUB);
        if (status != 0) {
            char message[256] {};
            (void)regerror(status, &value_, message, sizeof(message));
            throw Error(ErrorCode::invalid_definition,
                        "invalid strip exclusion pattern: " +
                            std::string(message));
        }
        valid_ = true;
    }

    ~BasicRegex()
    {
        if (valid_)
            regfree(&value_);
    }

    BasicRegex(const BasicRegex&) = delete;
    BasicRegex& operator=(const BasicRegex&) = delete;

    bool matches(const std::string& value) const
    {
        return regexec(&value_, value.c_str(), 0, nullptr, 0) == 0;
    }

private:
    regex_t value_ {};
    bool valid_{false};
};

std::vector<std::unique_ptr<BasicRegex>>
compile_exclusions(const std::vector<std::string>& patterns)
{
    std::vector<std::unique_ptr<BasicRegex>> result;
    result.reserve(patterns.size());
    for (const auto& pattern : patterns)
        result.push_back(std::make_unique<BasicRegex>(pattern));
    return result;
}

bool excluded(const std::vector<std::unique_ptr<BasicRegex>>& patterns,
              const std::filesystem::path& path)
{
    const auto value = path.generic_string();
    return std::any_of(patterns.begin(), patterns.end(),
                       [&](const auto& pattern) {
                           return pattern->matches(value);
                       });
}

std::pair<FileDescriptor, std::filesystem::path>
create_temporary_copy(const std::filesystem::path& input_path,
                      const ExecutionPolicy& execution)
{
    FileDescriptor input(open(input_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (input.get() < 0)
        filesystem_error("cannot open strip input", input_path);

    struct stat status {};
    if (fstat(input.get(), &status) != 0)
        filesystem_error("cannot inspect strip input", input_path);
    if (!S_ISREG(status.st_mode))
        throw Error(ErrorCode::transformation_failed,
                    "strip input is not a regular file: " + input_path.string());

    std::string pattern = (input_path.parent_path() /
                           (".pkgbuild-strip." +
                            input_path.filename().string() + ".XXXXXX")).string();
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    const int descriptor = mkstemp(storage.data());
    if (descriptor < 0)
        filesystem_error("cannot create strip temporary", input_path);
    FileDescriptor output(descriptor);
    const std::filesystem::path temporary(storage.data());

    try {
        if (execution.identity && geteuid() == 0 &&
            fchown(output.get(), execution.identity->uid,
                   execution.identity->gid) != 0)
            filesystem_error("cannot assign strip temporary", temporary);
        if (fchmod(output.get(), status.st_mode & 0777) != 0)
            filesystem_error("cannot set strip temporary mode", temporary);

        char buffer[64 * 1024];
        for (;;) {
            ssize_t count = 0;
            do {
                count = read(input.get(), buffer, sizeof(buffer));
            } while (count < 0 && errno == EINTR);
            if (count < 0)
                filesystem_error("cannot read strip input", input_path);
            if (count == 0)
                break;
            write_all(output.get(), reinterpret_cast<unsigned char*>(buffer),
                      static_cast<std::size_t>(count), temporary);
        }
    } catch (...) {
        (void)unlink(temporary.c_str());
        throw;
    }
    return {std::move(output), temporary};
}

std::filesystem::path backup_path(const std::filesystem::path& path,
                                  std::size_t sequence)
{
    return path.parent_path() /
           (".pkgbuild-backup." + path.filename().string() + "." +
            std::to_string(static_cast<long long>(getpid())) + "." +
            std::to_string(sequence));
}

void install_transformed_group(const StagedPackage& package,
                               const std::vector<std::size_t>& indices,
                               const std::filesystem::path& temporary)
{
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> backups;
    backups.reserve(indices.size());
    for (std::size_t item = 0; item != indices.size(); ++item) {
        const auto original = package.root / package.entries[indices[item]].path;
        const auto backup = backup_path(original, item);
        if (path_exists(backup))
            throw Error(ErrorCode::transformation_failed,
                        "strip backup already exists: " + backup.string());
        backups.emplace_back(original, backup);
    }

    std::size_t moved = 0;
    std::size_t installed = 0;
    try {
        for (; moved != backups.size(); ++moved) {
            if (rename(backups[moved].first.c_str(),
                       backups[moved].second.c_str()) != 0)
                filesystem_error("cannot back up strip input",
                                 backups[moved].first);
        }

        const auto canonical = backups.front().first;
        if (rename(temporary.c_str(), canonical.c_str()) != 0)
            filesystem_error("cannot install stripped file", canonical);
        installed = 1;

        for (; installed != backups.size(); ++installed) {
            if (link(canonical.c_str(), backups[installed].first.c_str()) != 0)
                filesystem_error("cannot relink stripped hardlink",
                                 backups[installed].first);
        }
    } catch (...) {
        (void)unlink(temporary.c_str());
        for (std::size_t item = 0; item != installed; ++item)
            (void)unlink(backups[item].first.c_str());
        for (std::size_t item = 0; item != moved; ++item) {
            if (path_exists(backups[item].second))
                (void)rename(backups[item].second.c_str(),
                             backups[item].first.c_str());
        }
        throw;
    }

    for (const auto& [original, backup] : backups) {
        (void)original;
        if (unlink(backup.c_str()) != 0)
            filesystem_error("cannot remove strip backup", backup);
    }
}

void strip_binaries(StagedPackage& package,
                    const PackageDefinition& definition,
                    const ExecutionPolicy& execution,
                    const std::filesystem::path& strip_program,
                    const ProcessExecutor& processes,
                    TransformationReceipt& receipt,
                    EventSink& events)
{
    if (strip_program.empty() || !strip_program.is_absolute())
        throw Error(ErrorCode::invalid_configuration,
                    "strip program must be an absolute path");

    const auto exclusions = compile_exclusions(definition.strip_exclusions);
    const auto groups = regular_groups(package);
    for (const auto& [canonical, indices] : groups) {
        const auto full = package.root / canonical;
        const auto mode = strip_mode(full);
        if (mode == StripMode::none)
            continue;

        if (std::any_of(indices.begin(), indices.end(), [&](std::size_t index) {
                return excluded(exclusions, package.entries[index].path);
            })) {
            emit(events, EventKind::info,
                 "Not stripping excluded hardlink group containing '" +
                     canonical.string() + "'");
            continue;
        }

        const auto identity = regular_identity(full);
        for (const auto index : indices) {
            const auto current = regular_identity(package.root /
                                                  package.entries[index].path);
            if (current.device != identity.device || current.inode != identity.inode)
                throw Error(ErrorCode::transformation_failed,
                            "staged hardlink group changed before stripping: " +
                                package.entries[index].path.string());
        }

        const auto bytes_before = package.entries[indices.front()].size;
        auto [temporary_descriptor, temporary] =
            create_temporary_copy(full, execution);
        (void)temporary_descriptor;

        emit(events, EventKind::command,
             "Stripping '" + canonical.string() + "'");
        const auto process = processes.execute(ProcessRequest{
            strip_program,
            {strip_option(mode), temporary.string()},
            package.root,
            execution.environment,
            execution.identity,
            execution.file_creation_mask,
            false,
            true,
        });
        if (!process.ok()) {
            (void)unlink(temporary.c_str());
            throw Error(ErrorCode::transformation_failed,
                        "strip failed for " + canonical.string() +
                            " with status " +
                            std::to_string(process.exit_status));
        }

        struct stat transformed {};
        if (lstat(temporary.c_str(), &transformed) != 0)
            filesystem_error("cannot inspect stripped file", temporary);
        if (!S_ISREG(transformed.st_mode))
            throw Error(ErrorCode::transformation_failed,
                        "strip output is not a regular file: " +
                            temporary.string());

        install_transformed_group(package, indices, temporary);
        const StagedTime time{
            static_cast<std::int64_t>(transformed.st_mtim.tv_sec),
            static_cast<std::uint32_t>(transformed.st_mtim.tv_nsec),
        };
        for (const auto index : indices) {
            package.entries[index].size =
                static_cast<std::uint64_t>(transformed.st_size);
            package.entries[index].modification_time = time;
        }

        std::vector<std::filesystem::path> paths;
        paths.reserve(indices.size());
        for (const auto index : indices)
            paths.push_back(package.entries[index].path);
        receipt.changes.push_back(TransformationChange{
            TransformationKind::strip_binary,
            paths,
            std::move(paths),
            bytes_before,
            static_cast<std::uintmax_t>(transformed.st_size),
        });
    }
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
    if (request.policy.strip_binaries)
        strip_binaries(request.package, request.definition, request.execution,
                       strip_program_, processes_, receipt, events);
    if (request.policy.compress_manpages)
        compress_manpages(request.package, receipt, events);
    validate_staged_package(request.package);
    return receipt;
}

} // namespace pkgbuild
