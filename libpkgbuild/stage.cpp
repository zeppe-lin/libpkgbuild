#include <pkgbuild/error.hpp>
#include <pkgbuild/stage.hpp>

#include "stage.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace pkgbuild {
namespace {

struct FileKey {
    std::uint64_t device;
    std::uint64_t inode;

    bool operator<(const FileKey& other) const noexcept
    {
        return device < other.device ||
               (device == other.device && inode < other.inode);
    }
};

bool canonical_package_path(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || path == "." ||
        path != path.lexically_normal())
        return false;
    for (const auto& component : path) {
        if (component.empty() || component == "." || component == "..")
            return false;
    }
    return true;
}

std::string read_symlink(const std::filesystem::path& path,
                         std::uint64_t expected_size)
{
    std::size_t capacity = static_cast<std::size_t>(expected_size) + 1;
    if (capacity < 256)
        capacity = 256;

    for (;;) {
        std::string result(capacity, '\0');
        const ssize_t size = readlink(path.c_str(), result.data(), result.size());
        if (size < 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot read staged symbolic link: " + path.string());
        if (static_cast<std::size_t>(size) < result.size()) {
            result.resize(static_cast<std::size_t>(size));
            return result;
        }
        if (capacity > 1024 * 1024)
            throw Error(ErrorCode::filesystem_failed,
                        "staged symbolic link target is too long: " +
                            path.string());
        capacity *= 2;
    }
}

StagedEntryType entry_type(const struct stat& status,
                           const std::filesystem::path& path)
{
    if (S_ISREG(status.st_mode)) return StagedEntryType::regular_file;
    if (S_ISDIR(status.st_mode)) return StagedEntryType::directory;
    if (S_ISLNK(status.st_mode)) return StagedEntryType::symbolic_link;
    if (S_ISFIFO(status.st_mode)) return StagedEntryType::fifo;
    if (S_ISCHR(status.st_mode)) return StagedEntryType::character_device;
    if (S_ISBLK(status.st_mode)) return StagedEntryType::block_device;
    throw Error(ErrorCode::filesystem_failed,
                "unsupported staged object type: " + path.string());
}

const char* type_name(StagedEntryType type)
{
    switch (type) {
    case StagedEntryType::regular_file: return "file";
    case StagedEntryType::directory: return "directory";
    case StagedEntryType::symbolic_link: return "symlink";
    case StagedEntryType::fifo: return "fifo";
    case StagedEntryType::character_device: return "character";
    case StagedEntryType::block_device: return "block";
    }
    return "unknown";
}

StagedEntryType type_from_name(const std::string& value)
{
    if (value == "file") return StagedEntryType::regular_file;
    if (value == "directory") return StagedEntryType::directory;
    if (value == "symlink") return StagedEntryType::symbolic_link;
    if (value == "fifo") return StagedEntryType::fifo;
    if (value == "character") return StagedEntryType::character_device;
    if (value == "block") return StagedEntryType::block_device;
    throw Error(ErrorCode::filesystem_failed,
                "unknown staged entry type: " + value);
}

template<typename T>
T parse_unsigned(const std::string& value, const char* description)
{
    static_assert(std::is_unsigned<T>::value, "unsigned integer required");
    T result{};
    const auto parsed = std::from_chars(value.data(),
                                        value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size())
        throw Error(ErrorCode::filesystem_failed,
                    std::string("invalid staged ") + description);
    return result;
}

std::int64_t parse_signed(const std::string& value, const char* description)
{
    std::int64_t result{};
    const auto parsed = std::from_chars(value.data(),
                                        value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size())
        throw Error(ErrorCode::filesystem_failed,
                    std::string("invalid staged ") + description);
    return result;
}

std::vector<std::string> read_nul_fields(const std::string& data)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin < data.size()) {
        const auto end = data.find('\0', begin);
        if (end == std::string::npos)
            throw Error(ErrorCode::filesystem_failed,
                        "staged metadata contains an unterminated field");
        fields.emplace_back(data.substr(begin, end - begin));
        begin = end + 1;
    }
    return fields;
}

void write_field(std::string& output, std::string_view value)
{
    output.append(value.data(), value.size());
    output.push_back('\0');
}

} // namespace

StagedPackage scan_staged_package(const std::filesystem::path& root_path)
{
    const auto root = std::filesystem::weakly_canonical(
        std::filesystem::absolute(root_path));
    if (!std::filesystem::is_directory(root))
        throw Error(ErrorCode::filesystem_failed,
                    "staged package root is not a directory: " + root.string());

    std::vector<std::filesystem::path> paths;
    for (std::filesystem::recursive_directory_iterator iterator(root), end;
         iterator != end; ++iterator)
        paths.push_back(iterator->path());
    std::sort(paths.begin(), paths.end(), [&](const auto& left, const auto& right) {
        return left.lexically_relative(root).generic_string() <
               right.lexically_relative(root).generic_string();
    });

    StagedPackage package;
    package.root = root;
    std::map<FileKey, std::filesystem::path> hardlinks;

    for (const auto& path : paths) {
        struct stat status {};
        if (lstat(path.c_str(), &status) != 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot inspect staged object: " + path.string());

        StagedEntry entry;
        entry.path = path.lexically_relative(root);
        entry.type = entry_type(status, path);
        entry.mode = static_cast<std::uint32_t>(status.st_mode & 07777);
        entry.uid = static_cast<std::uint64_t>(status.st_uid);
        entry.gid = static_cast<std::uint64_t>(status.st_gid);
        entry.modification_time = {
            static_cast<std::int64_t>(status.st_mtim.tv_sec),
            static_cast<std::uint32_t>(status.st_mtim.tv_nsec),
        };

        if (entry.type == StagedEntryType::regular_file) {
            entry.size = static_cast<std::uint64_t>(status.st_size);
            if (status.st_nlink > 1) {
                const FileKey key{
                    static_cast<std::uint64_t>(status.st_dev),
                    static_cast<std::uint64_t>(status.st_ino),
                };
                const auto [position, inserted] =
                    hardlinks.emplace(key, entry.path);
                if (!inserted)
                    entry.hardlink_target = position->second;
            }
        } else if (entry.type == StagedEntryType::symbolic_link) {
            entry.symlink_target = read_symlink(
                path, static_cast<std::uint64_t>(status.st_size));
        } else if (entry.type == StagedEntryType::character_device ||
                   entry.type == StagedEntryType::block_device) {
            entry.device = DeviceNumber{
                static_cast<std::uint64_t>(major(status.st_rdev)),
                static_cast<std::uint64_t>(minor(status.st_rdev)),
            };
        }
        package.entries.push_back(std::move(entry));
    }

    validate_staged_package(package);
    return package;
}

void validate_staged_package(const StagedPackage& package)
{
    if (package.root.empty() || !package.root.is_absolute() ||
        !std::filesystem::is_directory(package.root))
        throw Error(ErrorCode::invalid_configuration,
                    "staged package requires an absolute directory root");

    std::map<std::filesystem::path, const StagedEntry*> seen;
    std::filesystem::path previous;
    for (const auto& entry : package.entries) {
        if (!canonical_package_path(entry.path))
            throw Error(ErrorCode::invalid_configuration,
                        "invalid staged package path: " + entry.path.string());
        if (!previous.empty() &&
            previous.generic_string() >= entry.path.generic_string())
            throw Error(ErrorCode::invalid_configuration,
                        "staged package entries must be strictly sorted");
        previous = entry.path;
        if (entry.mode > 07777)
            throw Error(ErrorCode::invalid_configuration,
                        "invalid staged mode for " + entry.path.string());
        constexpr auto signed_limit =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (entry.uid > signed_limit || entry.gid > signed_limit ||
            entry.size > signed_limit ||
            (entry.device && (entry.device->major > signed_limit ||
                              entry.device->minor > signed_limit)))
            throw Error(ErrorCode::invalid_configuration,
                        "staged numeric metadata is out of range for " +
                            entry.path.string());
        if (entry.modification_time.nanoseconds >= 1000000000U)
            throw Error(ErrorCode::invalid_configuration,
                        "invalid staged timestamp for " + entry.path.string());

        const bool symlink = entry.type == StagedEntryType::symbolic_link;
        if (symlink != entry.symlink_target.has_value())
            throw Error(ErrorCode::invalid_configuration,
                        "invalid staged symlink metadata for " +
                            entry.path.string());

        const bool device = entry.type == StagedEntryType::character_device ||
                            entry.type == StagedEntryType::block_device;
        if (device != entry.device.has_value())
            throw Error(ErrorCode::invalid_configuration,
                        "invalid staged device metadata for " +
                            entry.path.string());

        if (entry.type != StagedEntryType::regular_file &&
            (entry.size != 0 || entry.hardlink_target))
            throw Error(ErrorCode::invalid_configuration,
                        "non-regular staged entry has file metadata: " +
                            entry.path.string());

        if (entry.hardlink_target) {
            const auto target = seen.find(*entry.hardlink_target);
            if (target == seen.end() ||
                target->second->type != StagedEntryType::regular_file ||
                target->second->hardlink_target)
                throw Error(ErrorCode::invalid_configuration,
                            "invalid staged hardlink target for " +
                                entry.path.string());
        }
        seen.emplace(entry.path, &entry);
    }
}

namespace detail {

std::string serialize_staged_manifest(const StagedPackage& package)
{
    validate_staged_package(package);
    std::string output;
    write_field(output, "pkgbuild-stage/1");
    write_field(output, std::to_string(package.entries.size()));
    for (const auto& entry : package.entries) {
        write_field(output, entry.path.generic_string());
        write_field(output, type_name(entry.type));
        write_field(output, std::to_string(entry.mode));
        write_field(output, std::to_string(entry.uid));
        write_field(output, std::to_string(entry.gid));
        write_field(output, std::to_string(entry.size));
        write_field(output, std::to_string(entry.modification_time.seconds));
        write_field(output, std::to_string(entry.modification_time.nanoseconds));
        write_field(output, entry.symlink_target.value_or(""));
        write_field(output, entry.device ? std::to_string(entry.device->major) : "");
        write_field(output, entry.device ? std::to_string(entry.device->minor) : "");
        write_field(output, entry.hardlink_target ?
                              entry.hardlink_target->generic_string() : "");
    }
    return output;
}

StagedPackage parse_staged_manifest(const std::filesystem::path& root,
                                    const std::string& data)
{
    const auto fields = read_nul_fields(data);
    if (fields.size() < 2 || fields[0] != "pkgbuild-stage/1")
        throw Error(ErrorCode::filesystem_failed,
                    "unsupported staged metadata format");
    const auto count = parse_unsigned<std::size_t>(fields[1], "entry count");
    constexpr std::size_t field_count = 12;
    if (count > (fields.size() - 2) / field_count ||
        fields.size() != 2 + count * field_count)
        throw Error(ErrorCode::filesystem_failed,
                    "staged metadata record count does not match header");

    StagedPackage package;
    package.root = std::filesystem::weakly_canonical(
        std::filesystem::absolute(root));
    package.entries.reserve(count);
    std::size_t index = 2;
    for (std::size_t item = 0; item != count; ++item) {
        StagedEntry entry;
        entry.path = fields[index++];
        entry.type = type_from_name(fields[index++]);
        entry.mode = parse_unsigned<std::uint32_t>(fields[index++], "mode");
        entry.uid = parse_unsigned<std::uint64_t>(fields[index++], "uid");
        entry.gid = parse_unsigned<std::uint64_t>(fields[index++], "gid");
        entry.size = parse_unsigned<std::uint64_t>(fields[index++], "size");
        entry.modification_time.seconds =
            parse_signed(fields[index++], "mtime seconds");
        entry.modification_time.nanoseconds =
            parse_unsigned<std::uint32_t>(fields[index++], "mtime nanoseconds");
        if (!fields[index].empty()) entry.symlink_target = fields[index];
        ++index;
        const std::string major_value = fields[index++];
        const std::string minor_value = fields[index++];
        if (!major_value.empty() || !minor_value.empty()) {
            if (major_value.empty() || minor_value.empty())
                throw Error(ErrorCode::filesystem_failed,
                            "incomplete staged device metadata");
            entry.device = DeviceNumber{
                parse_unsigned<std::uint64_t>(major_value, "device major"),
                parse_unsigned<std::uint64_t>(minor_value, "device minor"),
            };
        }
        if (!fields[index].empty()) entry.hardlink_target = fields[index];
        ++index;
        package.entries.push_back(std::move(entry));
    }
    validate_staged_package(package);
    return package;
}

} // namespace detail
} // namespace pkgbuild
