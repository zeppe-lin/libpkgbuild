#pragma once

#include <pkgbuild/types.hpp>

#include <filesystem>
#include <string>

namespace pkgbuild {

Footprint footprint_from_staged_package(const StagedPackage& package);
FootprintDifference compare_footprints(const Footprint& expected,
                                       const Footprint& actual);
Footprint read_footprint(const std::filesystem::path& path);
std::string serialize_footprint(const Footprint& footprint);
void write_footprint(const std::filesystem::path& path,
                     const Footprint& footprint);

} // namespace pkgbuild
