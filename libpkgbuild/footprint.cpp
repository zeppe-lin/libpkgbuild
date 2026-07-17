#include <pkgbuild/error.hpp>
#include <pkgbuild/footprint.hpp>
#include <pkgbuild/stage.hpp>

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <grp.h>
#include <limits>
#include <map>
#include <pwd.h>
#include <sstream>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

namespace pkgbuild {
namespace {

bool equal_entry(const FootprintEntry& left,
                 const FootprintEntry& right) noexcept
{
    return left.path == right.path &&
           left.type == right.type &&
           left.mode == right.mode &&
           left.uid == right.uid &&
           left.gid == right.gid &&
           left.symlink_target == right.symlink_target;
}

using EntryMap = std::map<std::filesystem::path, const FootprintEntry*>;

[[noreturn]] void invalid_line(std::size_t line, const std::string& message)
{
    throw Error(ErrorCode::invalid_footprint,
                "invalid footprint line " + std::to_string(line) +
                    ": " + message);
}

StagedEntryType type_from_character(char value, std::size_t line)
{
    switch (value) {
    case '-': return StagedEntryType::regular_file;
    case 'd': return StagedEntryType::directory;
    case 'l': return StagedEntryType::symbolic_link;
    case 'p': return StagedEntryType::fifo;
    case 'c': return StagedEntryType::character_device;
    case 'b': return StagedEntryType::block_device;
    default: invalid_line(line, "unknown object type");
    }
}

char type_character(StagedEntryType type)
{
    switch (type) {
    case StagedEntryType::regular_file: return '-';
    case StagedEntryType::directory: return 'd';
    case StagedEntryType::symbolic_link: return 'l';
    case StagedEntryType::fifo: return 'p';
    case StagedEntryType::character_device: return 'c';
    case StagedEntryType::block_device: return 'b';
    }
    throw Error(ErrorCode::invalid_footprint, "unknown footprint object type");
}

std::uint32_t parse_mode(std::string_view value, std::size_t line,
                         StagedEntryType& type)
{
    if (value.size() != 10)
        invalid_line(line, "permissions must contain ten characters");
    type = type_from_character(value[0], line);
    std::uint32_t mode = 0;
    const auto ordinary = [&](std::size_t index, char enabled,
                              std::uint32_t bit) {
        if (value[index] == enabled) mode |= bit;
        else if (value[index] != '-') invalid_line(line, "invalid permission");
    };
    ordinary(1, 'r', 0400);
    ordinary(2, 'w', 0200);
    if (value[3] == 'x' || value[3] == 's') mode |= 0100;
    if (value[3] == 's' || value[3] == 'S') mode |= 04000;
    if (value[3] != '-' && value[3] != 'x' && value[3] != 's' &&
        value[3] != 'S')
        invalid_line(line, "invalid user execute permission");

    ordinary(4, 'r', 0040);
    ordinary(5, 'w', 0020);
    if (value[6] == 'x' || value[6] == 's') mode |= 0010;
    if (value[6] == 's' || value[6] == 'S') mode |= 02000;
    if (value[6] != '-' && value[6] != 'x' && value[6] != 's' &&
        value[6] != 'S')
        invalid_line(line, "invalid group execute permission");

    ordinary(7, 'r', 0004);
    ordinary(8, 'w', 0002);
    if (value[9] == 'x' || value[9] == 't') mode |= 0001;
    if (value[9] == 't' || value[9] == 'T') mode |= 01000;
    if (value[9] != '-' && value[9] != 'x' && value[9] != 't' &&
        value[9] != 'T')
        invalid_line(line, "invalid other execute permission");
    return mode;
}

std::string format_mode(const FootprintEntry& entry)
{
    std::string result(10, '-');
    result[0] = type_character(entry.type);
    const auto mode = entry.mode;
    if (mode & 0400) result[1] = 'r';
    if (mode & 0200) result[2] = 'w';
    result[3] = mode & 04000 ? (mode & 0100 ? 's' : 'S')
                            : (mode & 0100 ? 'x' : '-');
    if (mode & 0040) result[4] = 'r';
    if (mode & 0020) result[5] = 'w';
    result[6] = mode & 02000 ? (mode & 0010 ? 's' : 'S')
                            : (mode & 0010 ? 'x' : '-');
    if (mode & 0004) result[7] = 'r';
    if (mode & 0002) result[8] = 'w';
    result[9] = mode & 01000 ? (mode & 0001 ? 't' : 'T')
                            : (mode & 0001 ? 'x' : '-');
    return result;
}

template<typename T>
std::optional<T> decimal_id(const std::string& value)
{
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(value.data(),
                                        value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} ||
        result.ptr != value.data() + value.size())
        return std::nullopt;
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
        return std::nullopt;
    return static_cast<T>(parsed);
}

std::uint64_t user_id(const std::string& value, std::size_t line)
{
    if (const auto numeric = decimal_id<uid_t>(value))
        return static_cast<std::uint64_t>(*numeric);
    std::vector<char> buffer(16384);
    passwd record {};
    passwd* found = nullptr;
    const int status = getpwnam_r(value.c_str(), &record, buffer.data(),
                                  buffer.size(), &found);
    if (status != 0 || found == nullptr)
        invalid_line(line, "unknown owner '" + value + "'");
    return static_cast<std::uint64_t>(record.pw_uid);
}

std::uint64_t group_id(const std::string& value, std::size_t line)
{
    if (const auto numeric = decimal_id<gid_t>(value))
        return static_cast<std::uint64_t>(*numeric);
    std::vector<char> buffer(16384);
    group record {};
    group* found = nullptr;
    const int status = getgrnam_r(value.c_str(), &record, buffer.data(),
                                  buffer.size(), &found);
    if (status != 0 || found == nullptr)
        invalid_line(line, "unknown group '" + value + "'");
    return static_cast<std::uint64_t>(record.gr_gid);
}

std::string user_name(std::uint64_t value)
{
    if (value > static_cast<std::uint64_t>(std::numeric_limits<uid_t>::max()))
        return std::to_string(value);
    std::vector<char> buffer(16384);
    passwd record {};
    passwd* found = nullptr;
    if (getpwuid_r(static_cast<uid_t>(value), &record, buffer.data(),
                   buffer.size(), &found) == 0 && found != nullptr)
        return record.pw_name;
    return std::to_string(value);
}

std::string group_name(std::uint64_t value)
{
    if (value > static_cast<std::uint64_t>(std::numeric_limits<gid_t>::max()))
        return std::to_string(value);
    std::vector<char> buffer(16384);
    group record {};
    group* found = nullptr;
    if (getgrgid_r(static_cast<gid_t>(value), &record, buffer.data(),
                   buffer.size(), &found) == 0 && found != nullptr)
        return record.gr_name;
    return std::to_string(value);
}

std::string trim_left(std::string value)
{
    const auto first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

FootprintEntry parse_entry(const std::string& line, std::size_t number)
{
    if (line.size() < 10)
        invalid_line(number, "line is too short");

    FootprintEntry entry;
    entry.mode = parse_mode(std::string_view(line).substr(0, 10), number,
                            entry.type);
    std::size_t position = line.find_first_not_of(" \t", 10);
    if (position == std::string::npos)
        invalid_line(number, "owner/group is missing");
    const auto owner_end = line.find_first_of(" \t", position);
    const std::string owner_group = line.substr(position, owner_end - position);
    const auto slash = owner_group.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash + 1 == owner_group.size() ||
        owner_group.find('/', slash + 1) != std::string::npos)
        invalid_line(number, "invalid owner/group field");
    entry.uid = user_id(owner_group.substr(0, slash), number);
    entry.gid = group_id(owner_group.substr(slash + 1), number);

    if (owner_end == std::string::npos)
        invalid_line(number, "path is missing");
    std::string remainder = trim_left(line.substr(owner_end));
    if (remainder.empty())
        invalid_line(number, "path is missing");

    if (entry.type == StagedEntryType::symbolic_link) {
        const auto arrow = remainder.find(" -> ");
        if (arrow == std::string::npos || arrow == 0 ||
            arrow + 4 == remainder.size())
            invalid_line(number, "symbolic link target is missing");
        entry.path = remainder.substr(0, arrow);
        entry.symlink_target = remainder.substr(arrow + 4);
    } else {
        entry.path = remainder;
        if (remainder.find(" -> ") != std::string::npos)
            invalid_line(number, "non-symbolic entry has a link target");
    }

    const bool trailing_slash = !entry.path.empty() &&
                                entry.path.native().back() == '/';
    if (entry.type == StagedEntryType::directory) {
        if (!trailing_slash)
            invalid_line(number, "directory path must end with '/'");
        entry.path = entry.path.parent_path();
    } else if (trailing_slash) {
        invalid_line(number, "non-directory path must not end with '/'");
    }
    return entry;
}

void write_all(int descriptor, const std::string& data,
               const std::filesystem::path& path)
{
    std::size_t offset = 0;
    while (offset != data.size()) {
        const ssize_t count = write(descriptor, data.data() + offset,
                                    data.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        throw Error(ErrorCode::filesystem_failed,
                    "cannot write footprint '" + path.string() + "': " +
                        std::strerror(errno));
    }
}

EntryMap index_entries(const Footprint& footprint)
{
    EntryMap result;
    for (const auto& entry : footprint.entries) {
        if (entry.path.empty() || entry.path.is_absolute() ||
            entry.path != entry.path.lexically_normal())
            throw Error(ErrorCode::invalid_definition,
                        "invalid footprint path: " + entry.path.string());
        const auto [position, inserted] = result.emplace(entry.path, &entry);
        if (!inserted)
            throw Error(ErrorCode::invalid_definition,
                        "duplicate footprint path: " +
                            position->first.string());
    }
    return result;
}

} // namespace

FootprintMismatch::FootprintMismatch(
    std::filesystem::path manifest, FootprintDifference difference)
    : Error(
          ErrorCode::footprint_mismatch,
          "footprint mismatch in '" + manifest.string() + "': " +
              std::to_string(difference.added.size()) + " added, " +
              std::to_string(difference.removed.size()) + " removed, " +
              std::to_string(difference.changed.size()) + " changed"),
      manifest_(std::move(manifest)),
      difference_(std::move(difference))
{
}

Footprint footprint_from_staged_package(const StagedPackage& package)
{
    validate_staged_package(package);

    Footprint result;
    result.entries.reserve(package.entries.size());
    for (const auto& staged : package.entries) {
        FootprintEntry entry;
        entry.path = staged.path;
        entry.type = staged.type;
        entry.mode = staged.mode;
        entry.uid = staged.uid;
        entry.gid = staged.gid;
        if (staged.type == StagedEntryType::symbolic_link)
            entry.symlink_target = staged.symlink_target;
        result.entries.push_back(std::move(entry));
    }

    std::sort(result.entries.begin(), result.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.path.generic_string() < right.path.generic_string();
              });
    (void)index_entries(result);
    return result;
}

FootprintDifference compare_footprints(const Footprint& expected,
                                       const Footprint& actual)
{
    const auto expected_entries = index_entries(expected);
    const auto actual_entries = index_entries(actual);

    FootprintDifference difference;
    for (const auto& [path, entry] : expected_entries) {
        const auto found = actual_entries.find(path);
        if (found == actual_entries.end()) {
            difference.removed.push_back(*entry);
        } else if (!equal_entry(*entry, *found->second)) {
            difference.changed.push_back(
                FootprintChange{*entry, *found->second});
        }
    }
    for (const auto& [path, entry] : actual_entries) {
        if (expected_entries.find(path) == expected_entries.end())
            difference.added.push_back(*entry);
    }
    return difference;
}

Footprint read_footprint(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot open footprint '" + path.string() + "'");

    Footprint footprint;
    std::string line;
    std::size_t number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        footprint.entries.push_back(parse_entry(line, number));
    }
    (void)index_entries(footprint);
    std::sort(footprint.entries.begin(), footprint.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.path.generic_string() < right.path.generic_string();
              });
    return footprint;
}

std::string serialize_footprint(const Footprint& footprint)
{
    const auto indexed = index_entries(footprint);
    std::ostringstream output;
    for (const auto& [path, entry] : indexed) {
        output << format_mode(*entry) << "    "
               << user_name(entry->uid) << '/' << group_name(entry->gid)
               << "    " << path.generic_string();
        if (entry->type == StagedEntryType::directory)
            output << '/';
        if (entry->type == StagedEntryType::symbolic_link) {
            if (!entry->symlink_target)
                throw Error(ErrorCode::invalid_footprint,
                            "symbolic footprint entry has no target: " +
                                path.string());
            output << " -> " << *entry->symlink_target;
        } else if (entry->symlink_target) {
            throw Error(ErrorCode::invalid_footprint,
                        "non-symbolic footprint entry has a target: " +
                            path.string());
        }
        output << '\n';
    }
    return output.str();
}

void write_footprint(const std::filesystem::path& path,
                     const Footprint& footprint)
{
    const auto data = serialize_footprint(footprint);
    const auto parent = path.has_parent_path() ? path.parent_path()
                                               : std::filesystem::path(".");
    std::filesystem::create_directories(parent);
    std::string pattern = (parent / (path.filename().string() +
                                     ".tmp.XXXXXX")).string();
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    const int descriptor = mkstemp(storage.data());
    if (descriptor < 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot create temporary footprint beside '" +
                        path.string() + "': " + std::strerror(errno));
    const std::filesystem::path temporary(storage.data());
    bool renamed = false;
    try {
        if (fchmod(descriptor, 0644) != 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot set footprint permissions: " +
                            std::string(std::strerror(errno)));
        write_all(descriptor, data, temporary);
        if (fsync(descriptor) != 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot sync footprint '" + temporary.string() +
                            "': " + std::strerror(errno));
        if (close(descriptor) != 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot close footprint '" + temporary.string() +
                            "': " + std::strerror(errno));
        if (rename(temporary.c_str(), path.c_str()) != 0)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot replace footprint '" + path.string() +
                            "': " + std::strerror(errno));
        renamed = true;
    } catch (...) {
        if (!renamed) {
            (void)close(descriptor);
            (void)unlink(temporary.c_str());
        }
        throw;
    }
}

} // namespace pkgbuild
