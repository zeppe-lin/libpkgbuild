#pragma once

#include <pkgbuild/types.hpp>

#include <filesystem>
#include <vector>

namespace pkgbuild {

class VerifiedSource final {
public:
    VerifiedSource(int descriptor,
                   std::filesystem::path path,
                   std::vector<VerificationReceipt> receipts);
    ~VerifiedSource();

    VerifiedSource(const VerifiedSource&) = delete;
    VerifiedSource& operator=(const VerifiedSource&) = delete;

    VerifiedSource(VerifiedSource&& other) noexcept;
    VerifiedSource& operator=(VerifiedSource&& other) noexcept;

    const std::filesystem::path& path() const noexcept;
    const std::vector<VerificationReceipt>& receipts() const noexcept;

    int duplicate_descriptor() const;

private:
    int descriptor_{-1};
    std::filesystem::path path_;
    std::vector<VerificationReceipt> receipts_;
};

} // namespace pkgbuild
