#pragma once

#include <pkgbuild/types.hpp>

#include <filesystem>
#include <string>

namespace pkgbuild::detail {

std::string serialize_staged_manifest(const StagedPackage& package);
StagedPackage parse_staged_manifest(const std::filesystem::path& root,
                                    const std::string& data);

} // namespace pkgbuild::detail
