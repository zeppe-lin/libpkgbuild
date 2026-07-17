#pragma once

#include <pkgbuild/types.hpp>

#include <filesystem>

namespace pkgbuild {

StagedPackage scan_staged_package(const std::filesystem::path& root);
void validate_staged_package(const StagedPackage& package);

} // namespace pkgbuild
