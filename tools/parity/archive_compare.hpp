#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pkgbuild::parity {

struct ArchiveDifference {
    std::string path;
    std::string field;
    std::string reference;
    std::string candidate;
};

struct ArchiveComparison {
    std::filesystem::path reference_archive;
    std::filesystem::path candidate_archive;
    std::vector<ArchiveDifference> differences;

    bool equivalent() const noexcept
    {
        return differences.empty();
    }
};

ArchiveComparison compare_archives(
    const std::filesystem::path& reference,
    const std::filesystem::path& candidate);

std::string format_difference(const ArchiveDifference& difference);

} // namespace pkgbuild::parity
