#include <pkgbuild/error.hpp>
#include <pkgbuild/footprint.hpp>
#include <pkgbuild/stage.hpp>

#include <algorithm>
#include <map>

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

} // namespace pkgbuild
