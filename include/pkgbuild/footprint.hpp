#pragma once

#include <pkgbuild/types.hpp>

namespace pkgbuild {

Footprint footprint_from_staged_package(const StagedPackage& package);
FootprintDifference compare_footprints(const Footprint& expected,
                                       const Footprint& actual);

} // namespace pkgbuild
