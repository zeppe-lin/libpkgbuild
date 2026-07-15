#pragma once

#include <stdexcept>
#include <string>

namespace pkgbuild {

enum class ErrorCode {
    invalid_definition,
    invalid_configuration,
    missing_source,
    download_failed,
    extraction_failed,
    recipe_failed,
    archive_failed,
    process_failed,
    filesystem_failed,
};

class Error final : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

} // namespace pkgbuild
