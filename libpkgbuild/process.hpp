#pragma once

#include <string>
#include <vector>

namespace pkgbuild::detail {

std::vector<std::string> split_nul(const std::string& data);

} // namespace pkgbuild::detail
