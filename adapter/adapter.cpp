// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pkgbuild-plan/adapter.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/evp.h>

#include <libpkgimage/error.h>
#include <libpkgplan/digest.h>

namespace pkgbuild::plan_adapter {
namespace {

class canonical_record final {
public:
    void u64(std::uint64_t value)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void text(std::string_view value)
    {
        u64(static_cast<std::uint64_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept
    {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

[[nodiscard]] pkgplan::sha256_digest_bytes sha256(const canonical_record& record)
{
    pkgplan::sha256_digest_bytes digest{};
    unsigned int size = 0;
    EVP_MD_CTX* raw = EVP_MD_CTX_new();
    if (raw == nullptr)
        throw projection_error(projection_error_code::planner_fact,
                               "cannot allocate build planning digest context");
    struct context final {
        EVP_MD_CTX* value;
        ~context() { EVP_MD_CTX_free(value); }
    } holder{raw};

    if (EVP_DigestInit_ex(holder.value, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(holder.value, record.bytes().data(),
                         record.bytes().size()) != 1 ||
        EVP_DigestFinal_ex(holder.value, digest.data(), &size) != 1 ||
        size != digest.size())
        throw projection_error(projection_error_code::planner_fact,
                               "cannot compute build planning identity");
    return digest;
}

[[nodiscard]] bool lowercase_hex(std::string_view value) noexcept
{
    if (value.size() != 64)
        return false;
    for (const unsigned char byte : value) {
        if (!std::isdigit(byte) && !(byte >= 'a' && byte <= 'f'))
            return false;
    }
    return true;
}

void validate_build_receipt(const pkgbuild::BuildReceipt& build)
{
    const auto package = std::filesystem::absolute(build.package).lexically_normal();
    const auto archive =
        std::filesystem::absolute(build.archive.output).lexically_normal();
    const auto artifact =
        std::filesystem::absolute(build.artifact.path).lexically_normal();

    if (build.package.empty() || build.archive.output.empty() ||
        build.artifact.path.empty() || package != archive || package != artifact)
        throw projection_error(projection_error_code::build_receipt,
                               "build receipt does not bind one artifact path");
    if (build.archive.bytes_written != build.artifact.bytes)
        throw projection_error(projection_error_code::build_receipt,
                               "build receipt artifact byte counts disagree");
    if (build.archive.archive.format != build.definition.policy().archive.format ||
        build.archive.archive.compression !=
            build.definition.policy().archive.compression)
        throw projection_error(projection_error_code::build_receipt,
                               "build receipt archive policy mismatch");
    if (build.artifact.digest.algorithm != pkgbuild::DigestAlgorithm::sha256 ||
        !lowercase_hex(build.artifact.digest.hexadecimal))
        throw projection_error(projection_error_code::build_receipt,
                               "build receipt lacks a canonical SHA-256 artifact seal");
}

[[nodiscard]] pkgimage::complete_archive_digest
expected_archive(const pkgbuild::SealedArtifactReceipt& artifact)
{
    return pkgimage::complete_archive_digest::parse(
        "v1:sha256:" + artifact.digest.hexadecimal);
}

[[nodiscard]] pkgplan::artifact_manifest_identity manifest_identity(
    const pkgbuild::BuildReceipt& build,
    const pkgsource::plan_adapter::candidate_projection& candidate,
    const pkgplan::artifact_identity& artifact,
    const pkgimage::inspected_package_image& image)
{
    canonical_record record;
    record.text("libpkgbuild-plan/artifact-manifest/v1");
    record.text(pkgsource::to_string(
        build.definition.source_snapshot_fingerprint().algorithm()));
    record.text(build.definition.source_snapshot_fingerprint().hex());
    record.text(candidate.candidate().release().identity().string());
    record.text(candidate.candidate().identity().string());
    record.text(artifact.string());
    record.text(image.image().identity().string());
    record.text(image.receipt().identity().string());
    record.u64(image.receipt().entry_count());
    return pkgplan::artifact_manifest_identity::from_sha256(sha256(record));
}

void validate_coordinates(
    const pkgbuild::BuildReceipt& build,
    const pkgsource::plan_adapter::candidate_projection& candidate)
{
    const auto& built = build.definition.identity();
    const auto& release = candidate.candidate().release();
    if (built.name != release.name() || built.version != release.version() ||
        built.release != release.release())
        throw projection_error(projection_error_code::source_projection,
                               "build definition and source candidate releases differ");
    if (build.definition.source_snapshot_fingerprint().algorithm() !=
            candidate.source_fingerprint().algorithm() ||
        build.definition.source_snapshot_fingerprint().hex() !=
            candidate.source_fingerprint().hex())
        throw projection_error(projection_error_code::source_projection,
                               "build definition and candidate snapshots differ");
}

} // namespace

projection_error::projection_error(projection_error_code code, std::string message)
    : std::runtime_error(std::move(message)), code_(code)
{
}

projection_error_code projection_error::code() const noexcept
{
    return code_;
}

artifact_projection::artifact_projection(
    pkgbuild::BuildReceipt build,
    pkgsource::plan_adapter::candidate_projection candidate,
    pkgimage::inspected_package_image image,
    pkgplan::artifact_package_fact artifact)
    : build_(std::move(build)),
      candidate_(std::move(candidate)),
      image_(std::move(image)),
      artifact_(std::move(artifact))
{
}

const pkgbuild::BuildReceipt& artifact_projection::build() const noexcept
{
    return build_;
}

const pkgsource::plan_adapter::candidate_projection&
artifact_projection::candidate() const noexcept
{
    return candidate_;
}

const pkgimage::inspected_package_image&
artifact_projection::image() const noexcept
{
    return image_;
}

const pkgplan::artifact_package_fact&
artifact_projection::artifact() const noexcept
{
    return artifact_;
}

artifact_projection project_artifact(
    pkgbuild::BuildReceipt build,
    const pkgimage::archive_backend& archives)
{
    validate_build_receipt(build);

    pkgsource::plan_adapter::candidate_projection candidate = [&]() {
        try {
            return pkgsource::plan_adapter::project_candidate(
                build.definition.snapshot());
        } catch (const pkgsource::plan_adapter::projection_error& error) {
            throw projection_error(
                projection_error_code::source_projection,
                std::string("cannot project build source candidate: ") +
                    error.what());
        }
    }();
    validate_coordinates(build, candidate);

    pkgimage::inspected_package_image image = [&]() {
        try {
            return archives.inspect(pkgimage::archive_inspection_request{
                build.artifact.path, expected_archive(build.artifact)});
        } catch (const pkgimage::error& error) {
            throw projection_error(
                projection_error_code::archive_inspection,
                std::string("cannot inspect sealed build artifact: ") +
                    error.what());
        }
    }();

    try {
        const pkgplan::artifact_identity artifact =
            pkgplan::artifact_identity::parse(
                image.receipt().archive_digest().string());
        const pkgplan::artifact_manifest_identity manifest =
            manifest_identity(build, candidate, artifact, image);
        pkgplan::artifact_package_fact artifact_fact(
            artifact, manifest, candidate.candidate().release());
        return artifact_projection(std::move(build), std::move(candidate),
                                   std::move(image), std::move(artifact_fact));
    } catch (const projection_error&) {
        throw;
    } catch (const std::exception& error) {
        throw projection_error(
            projection_error_code::planner_fact,
            std::string("planner rejected build artifact projection: ") +
                error.what());
    }
}

} // namespace pkgbuild::plan_adapter
