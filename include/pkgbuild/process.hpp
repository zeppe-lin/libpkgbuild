#pragma once

#include <pkgbuild/types.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pkgbuild {

struct ProcessRequest {
    std::filesystem::path program;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::map<std::string, std::string> environment;
    std::optional<BuildIdentity> identity;
    mode_t file_creation_mask{0022};
    bool capture_stdout{false};
    bool create_process_group{true};
};

struct ProcessResult {
    int exit_status{0};
    int termination_signal{0};
    std::string stdout_data;

    bool ok() const noexcept
    {
        return exit_status == 0 && termination_signal == 0;
    }
};

void validate_execution_policy(const ExecutionPolicy& policy);

class ProcessExecutor {
public:
    virtual ~ProcessExecutor() = default;
    virtual ProcessResult execute(const ProcessRequest& request) const = 0;
};

} // namespace pkgbuild
