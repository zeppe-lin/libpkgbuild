#pragma once

#include <stdexcept>
#include <string>

namespace pkgbuild {

enum class ErrorCode {
    invalid_definition,
    invalid_configuration,
    missing_source,
    download_failed,
    invalid_digest,
    checksum_mismatch,
    source_changed,
    extraction_failed,
    recipe_failed,
    transformation_failed,
    invalid_footprint,
    footprint_mismatch,
    archive_failed,
    process_failed,
    filesystem_failed,
};

class Error : public std::runtime_error {
public:
    Error(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

} // namespace pkgbuild
