#pragma once

#include <pkgbuild/error.hpp>
#include <pkgbuild/types.hpp>

#include <filesystem>
#include <string>

namespace pkgbuild {

class FootprintMismatch final : public Error {
public:
    FootprintMismatch(std::filesystem::path manifest,
                      FootprintDifference difference);

    const std::filesystem::path& manifest() const noexcept
    {
        return manifest_;
    }

    const FootprintDifference& difference() const noexcept
    {
        return difference_;
    }

private:
    std::filesystem::path manifest_;
    FootprintDifference difference_;
};

Footprint footprint_from_staged_package(const StagedPackage& package);
FootprintDifference compare_footprints(const Footprint& expected,
                                       const Footprint& actual);
Footprint read_footprint(const std::filesystem::path& path);
std::string serialize_footprint(const Footprint& footprint);
void write_footprint(const std::filesystem::path& path,
                     const Footprint& footprint);

} // namespace pkgbuild
