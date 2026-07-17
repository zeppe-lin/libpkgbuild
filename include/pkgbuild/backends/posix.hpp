#pragma once

#include <pkgbuild/process.hpp>

namespace pkgbuild {

class PosixProcessExecutor final : public ProcessExecutor {
public:
    ProcessResult execute(const ProcessRequest& request) const override;
};

} // namespace pkgbuild
