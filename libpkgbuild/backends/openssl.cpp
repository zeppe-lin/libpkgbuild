#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/error.hpp>

#include <openssl/evp.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pkgbuild {
namespace {

class FileDescriptor final
{
public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor()
    {
        if (value_ >= 0)
            (void)close(value_);
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const noexcept { return value_; }
    int release() noexcept
    {
        const int result = value_;
        value_ = -1;
        return result;
    }

private:
    int value_;
};

struct ContextDeleter {
    void operator()(EVP_MD_CTX* value) const noexcept
    {
        EVP_MD_CTX_free(value);
    }
};

using ContextPtr = std::unique_ptr<EVP_MD_CTX, ContextDeleter>;

struct DigestContext {
    Digest expected;
    ContextPtr context;
};

const EVP_MD* method_for(DigestAlgorithm algorithm)
{
    switch (algorithm) {
    case DigestAlgorithm::md5: return EVP_md5();
    case DigestAlgorithm::sha256: return EVP_sha256();
    case DigestAlgorithm::sha512: return EVP_sha512();
    case DigestAlgorithm::blake2b512: return EVP_blake2b512();
    }
    return nullptr;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

Digest normalize_digest(const Digest& digest)
{
    const EVP_MD* method = method_for(digest.algorithm);
    if (method == nullptr)
        throw Error(ErrorCode::invalid_digest,
                    "unsupported digest algorithm: " +
                        to_string(digest.algorithm));

    const int bytes = EVP_MD_get_size(method);
    if (bytes <= 0)
        throw Error(ErrorCode::invalid_digest,
                    "digest algorithm has no fixed output size: " +
                        to_string(digest.algorithm));

    Digest result{digest.algorithm, lowercase(digest.hexadecimal)};
    const std::size_t expected_size = static_cast<std::size_t>(bytes) * 2;
    if (result.hexadecimal.size() != expected_size)
        throw Error(ErrorCode::invalid_digest,
                    "invalid " + to_string(result.algorithm) +
                        " digest length: expected " +
                        std::to_string(expected_size) + " hexadecimal characters");

    if (!std::all_of(result.hexadecimal.begin(), result.hexadecimal.end(),
                     [](unsigned char character) {
                         return std::isxdigit(character) != 0;
                     }))
        throw Error(ErrorCode::invalid_digest,
                    "invalid hexadecimal " + to_string(result.algorithm) +
                        " digest");
    return result;
}

bool same_source(const struct stat& left, const struct stat& right)
{
    return left.st_dev == right.st_dev &&
           left.st_ino == right.st_ino &&
           left.st_size == right.st_size &&
           left.st_mtim.tv_sec == right.st_mtim.tv_sec &&
           left.st_mtim.tv_nsec == right.st_mtim.tv_nsec;
}

std::string hex(const unsigned char* data, unsigned int size)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index != size; ++index)
        output << std::setw(2) << static_cast<unsigned int>(data[index]);
    return output.str();
}

std::vector<std::pair<Digest, std::string>>
digest_descriptor(int descriptor,
                  const std::vector<Digest>& requested,
                  const std::filesystem::path& display_path)
{
    if (requested.empty())
        throw Error(ErrorCode::invalid_digest,
                    "source has no declared digest: " +
                        display_path.string());

    struct stat before {};
    if (fstat(descriptor, &before) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot inspect source '" + display_path.string() +
                        "': " + std::strerror(errno));
    if (!S_ISREG(before.st_mode))
        throw Error(ErrorCode::filesystem_failed,
                    "source is not a regular file: " +
                        display_path.string());

    std::vector<DigestContext> contexts;
    contexts.reserve(requested.size());
    std::set<DigestAlgorithm> algorithms;
    for (const auto& item : requested) {
        Digest normalized = normalize_digest(item);
        if (!algorithms.insert(normalized.algorithm).second)
            throw Error(ErrorCode::invalid_digest,
                        "duplicate " + to_string(normalized.algorithm) +
                            " digest for " + display_path.string());
        const EVP_MD* method = method_for(normalized.algorithm);
        ContextPtr context(EVP_MD_CTX_new());
        if (!context || EVP_DigestInit_ex(context.get(), method, nullptr) != 1)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot initialize " + to_string(normalized.algorithm) +
                            " verification for " + display_path.string());
        contexts.push_back(
            DigestContext{std::move(normalized), std::move(context)});
    }

    char buffer[64 * 1024];
    off_t offset = 0;
    for (;;) {
        const ssize_t count = pread(descriptor, buffer, sizeof(buffer), offset);
        if (count > 0) {
            for (auto& context : contexts) {
                if (EVP_DigestUpdate(context.context.get(), buffer,
                                     static_cast<std::size_t>(count)) != 1)
                    throw Error(ErrorCode::filesystem_failed,
                                "cannot update " +
                                    to_string(context.expected.algorithm) +
                                    " verification for " +
                                    display_path.string());
            }
            offset += count;
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        throw Error(ErrorCode::filesystem_failed,
                    "cannot read source '" + display_path.string() +
                        "': " + std::strerror(errno));
    }

    struct stat after {};
    if (fstat(descriptor, &after) != 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot recheck source '" + display_path.string() +
                        "': " + std::strerror(errno));
    if (!same_source(before, after))
        throw Error(ErrorCode::source_changed,
                    "source changed during verification: " +
                        display_path.string());

    std::vector<std::pair<Digest, std::string>> results;
    results.reserve(contexts.size());
    for (auto& context : contexts) {
        unsigned char value[EVP_MAX_MD_SIZE];
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context.context.get(), value, &size) != 1)
            throw Error(ErrorCode::filesystem_failed,
                        "cannot finish " +
                            to_string(context.expected.algorithm) +
                            " verification for " + display_path.string());
        results.emplace_back(std::move(context.expected), hex(value, size));
    }
    return results;
}

} // namespace

VerifiedSource OpenSslSourceVerifier::verify(
    const std::filesystem::path& source,
    const std::vector<Digest>& digests,
    EventSink& events) const
{
    const auto display = std::filesystem::absolute(source).lexically_normal();
    emit(events, EventKind::info,
         "Verifying source '" + display.string() + "' with " +
             std::string(name()));

    FileDescriptor descriptor(open(display.c_str(),
                                   O_RDONLY | O_CLOEXEC | O_NONBLOCK));
    if (descriptor.get() < 0)
        throw Error(ErrorCode::filesystem_failed,
                    "cannot open source '" + display.string() + "': " +
                        std::strerror(errno));

    const auto observed = digest_descriptor(descriptor.get(), digests, display);
    std::vector<VerificationReceipt> receipts;
    receipts.reserve(observed.size());
    for (const auto& [expected, value] : observed) {
        if (value != expected.hexadecimal)
            throw Error(ErrorCode::checksum_mismatch,
                        to_string(expected.algorithm) +
                            " checksum mismatch for '" + display.string() +
                            "': expected " + expected.hexadecimal +
                            ", observed " + value);
        receipts.push_back(VerificationReceipt{display, expected, value});
    }

    return VerifiedSource(descriptor.release(), display, std::move(receipts));
}

void OpenSslSourceVerifier::revalidate(const VerifiedSource& source,
                                       EventSink& events) const
{
    emit(events, EventKind::info,
         "Rechecking verified source '" + source.path().string() + "'");

    std::vector<Digest> expected;
    expected.reserve(source.receipts().size());
    for (const auto& receipt : source.receipts())
        expected.push_back(receipt.expected);

    FileDescriptor descriptor(source.duplicate_descriptor());
    const auto observed =
        digest_descriptor(descriptor.get(), expected, source.path());
    if (observed.size() != source.receipts().size())
        throw Error(ErrorCode::source_changed,
                    "source verification record changed: " +
                        source.path().string());

    for (std::size_t index = 0; index != observed.size(); ++index) {
        if (observed[index].second != source.receipts()[index].observed)
            throw Error(ErrorCode::source_changed,
                        "verified source changed before use: " +
                            source.path().string());
    }
}

} // namespace pkgbuild
