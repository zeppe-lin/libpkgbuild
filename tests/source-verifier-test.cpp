#include <pkgbuild/backends/openssl.hpp>
#include <pkgbuild/error.hpp>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool value, const std::string& message)
{
    if (!value)
        fail(message);
}

template<typename Function>
void require_error(pkgbuild::ErrorCode code, Function function)
{
    try {
        function();
    } catch (const pkgbuild::Error& error) {
        require(error.code() == code,
                "unexpected pkgbuild error code for: " +
                    std::string(error.what()));
        return;
    }
    fail("expected pkgbuild error");
}

std::filesystem::path temporary_directory()
{
    std::string pattern = "/tmp/libpkgbuild-verifier.XXXXXX";
    std::vector<char> storage(pattern.begin(), pattern.end());
    storage.push_back('\0');
    char* result = mkdtemp(storage.data());
    if (!result)
        fail("mkdtemp failed");
    return result;
}

void write_file(const std::filesystem::path& path, const std::string& value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        fail("cannot write test file");
    output << value;
}

std::string read_descriptor(int descriptor)
{
    if (lseek(descriptor, 0, SEEK_SET) < 0)
        fail("cannot rewind duplicate descriptor");
    std::string result;
    char buffer[128];
    for (;;) {
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            result.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        fail("cannot read duplicate descriptor");
    }
    return result;
}

} // namespace

int main()
{
    std::filesystem::path root;
    try {
        root = temporary_directory();
        const auto source = root / "source";
        write_file(source, "abc");

        const std::vector<pkgbuild::Digest> digests = {
            {pkgbuild::DigestAlgorithm::md5,
             "900150983CD24FB0D6963F7D28E17F72"},
            {pkgbuild::DigestAlgorithm::sha256,
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
            {pkgbuild::DigestAlgorithm::sha512,
             "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
             "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
            {pkgbuild::DigestAlgorithm::blake2b512,
             "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
             "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"},
        };

        pkgbuild::OpenSslSourceVerifier verifier;
        pkgbuild::NullEventSink events;
        auto verified = verifier.verify(source, digests, events);
        require(verified.receipts().size() == digests.size(),
                "not all digest receipts were recorded");
        require(verified.receipts().front().expected.hexadecimal ==
                    "900150983cd24fb0d6963f7d28e17f72",
                "digest was not normalized");

        int duplicate = verified.duplicate_descriptor();
        require(read_descriptor(duplicate) == "abc",
                "verified descriptor returned wrong bytes");
        close(duplicate);

        const auto retained = root / "retained";
        std::filesystem::rename(source, retained);
        write_file(source, "replacement");
        duplicate = verified.duplicate_descriptor();
        require(read_descriptor(duplicate) == "abc",
                "pathname replacement changed verified descriptor bytes");
        close(duplicate);
        verifier.revalidate(verified, events);

        write_file(retained, "changed");
        require_error(pkgbuild::ErrorCode::source_changed, [&] {
            verifier.revalidate(verified, events);
        });

        write_file(root / "mismatch", "abc");
        require_error(pkgbuild::ErrorCode::checksum_mismatch, [&] {
            (void)verifier.verify(
                root / "mismatch",
                {{pkgbuild::DigestAlgorithm::md5,
                  "00000000000000000000000000000000"}},
                events);
        });
        require_error(pkgbuild::ErrorCode::invalid_digest, [&] {
            (void)verifier.verify(
                root / "mismatch",
                {{pkgbuild::DigestAlgorithm::sha256, "abcd"}}, events);
        });
        require_error(pkgbuild::ErrorCode::invalid_digest, [&] {
            (void)verifier.verify(
                root / "mismatch",
                {{pkgbuild::DigestAlgorithm::md5,
                  "z00150983cd24fb0d6963f7d28e17f72"}},
                events);
        });
        require_error(pkgbuild::ErrorCode::invalid_digest, [&] {
            (void)verifier.verify(
                root / "mismatch",
                {{pkgbuild::DigestAlgorithm::md5,
                  "900150983cd24fb0d6963f7d28e17f72"},
                 {pkgbuild::DigestAlgorithm::md5,
                  "900150983cd24fb0d6963f7d28e17f72"}},
                events);
        });

        const auto fifo = root / "fifo";
        require(mkfifo(fifo.c_str(), 0600) == 0, "mkfifo failed");
        require_error(pkgbuild::ErrorCode::filesystem_failed, [&] {
            (void)verifier.verify(
                fifo,
                {{pkgbuild::DigestAlgorithm::md5,
                  "d41d8cd98f00b204e9800998ecf8427e"}},
                events);
        });

        std::filesystem::remove_all(root);
        std::cout << "source verifier: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        if (!root.empty())
            std::filesystem::remove_all(root);
        std::cerr << "source verifier: " << error.what() << '\n';
        return 1;
    }
}
