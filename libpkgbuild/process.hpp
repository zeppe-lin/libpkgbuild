#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pkgbuild::detail {

struct ProcessResult {
    int exit_status{0};
    std::string stdout_data;
};

ProcessResult run_capture(const std::filesystem::path& program,
                          const std::vector<std::string>& arguments);

int run_inherit(const std::filesystem::path& program,
                const std::vector<std::string>& arguments);

std::vector<std::string> split_nul(const std::string& data);

} // namespace pkgbuild::detail
