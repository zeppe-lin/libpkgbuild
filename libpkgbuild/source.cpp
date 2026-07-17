#include <pkgbuild/error.hpp>
#include <pkgbuild/source.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace pkgbuild {

VerifiedSource::VerifiedSource(
    int descriptor,
    std::filesystem::path path,
    std::vector<VerificationReceipt> receipts)
    : descriptor_(descriptor),
      path_(std::move(path)),
      receipts_(std::move(receipts))
{
    if (descriptor_ < 0)
        throw Error(ErrorCode::filesystem_failed,
                    "verified source has no file descriptor");
}

VerifiedSource::~VerifiedSource()
{
    if (descriptor_ >= 0)
        (void)close(descriptor_);
}

VerifiedSource::VerifiedSource(VerifiedSource&& other) noexcept
    : descriptor_(other.descriptor_),
      path_(std::move(other.path_)),
      receipts_(std::move(other.receipts_))
{
    other.descriptor_ = -1;
}

VerifiedSource& VerifiedSource::operator=(VerifiedSource&& other) noexcept
{
    if (this != &other) {
        if (descriptor_ >= 0)
            (void)close(descriptor_);
        descriptor_ = other.descriptor_;
        path_ = std::move(other.path_);
        receipts_ = std::move(other.receipts_);
        other.descriptor_ = -1;
    }
    return *this;
}

const std::filesystem::path& VerifiedSource::path() const noexcept
{
    return path_;
}

const std::vector<VerificationReceipt>&
VerifiedSource::receipts() const noexcept
{
    return receipts_;
}

int VerifiedSource::duplicate_descriptor() const
{
    if (descriptor_ < 0)
        throw Error(ErrorCode::filesystem_failed,
                    "verified source descriptor is closed");

    const int duplicate = fcntl(descriptor_, F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot duplicate verified source descriptor for " +
                        path_.string() + ": " + std::strerror(errno));
    return duplicate;
}

} // namespace pkgbuild
