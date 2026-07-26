// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild-plan/adapter.h>

#include <libpkgimage/error.h>
#include <libpkgplan/digest.h>

#include <openssl/evp.h>

#include <array>
#include <cstdint>
#include <system_error>
#include <utility>
#include <vector>

namespace pkgbuild::plan_adapter {
namespace {

class canonical_record final {
public:
  void number(std::uint64_t value)
  {
    for (int shift = 56; shift >= 0; shift -= 8)
      bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
  }
  void text(std::string_view value)
  {
    number(value.size());
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept
  {
    return bytes_;
  }
private:
  std::vector<std::uint8_t> bytes_;
};

pkgplan::sha256_digest_bytes sha256(const canonical_record& record)
{
  pkgplan::sha256_digest_bytes digest{};
  unsigned int size = 0;
  EVP_MD_CTX* raw = EVP_MD_CTX_new();
  if (!raw)
    throw projection_error(projection_error_code::planner_fact,
                           "cannot allocate planner digest context");
  struct holder final {
    EVP_MD_CTX* value;
    ~holder() { EVP_MD_CTX_free(value); }
  } context{raw};
  if (EVP_DigestInit_ex(context.value, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context.value, record.bytes().data(),
                       record.bytes().size()) != 1 ||
      EVP_DigestFinal_ex(context.value, digest.data(), &size) != 1 ||
      size != digest.size())
    throw projection_error(projection_error_code::planner_fact,
                           "cannot compute planner manifest identity");
  return digest;
}

pkgimage::entry_type image_type(payload_entry_type value)
{
  switch (value) {
  case payload_entry_type::regular: return pkgimage::entry_type::regular;
  case payload_entry_type::directory: return pkgimage::entry_type::directory;
  case payload_entry_type::symlink: return pkgimage::entry_type::symlink;
  case payload_entry_type::hardlink: return pkgimage::entry_type::hardlink;
  case payload_entry_type::fifo: return pkgimage::entry_type::fifo;
  case payload_entry_type::character_device:
    return pkgimage::entry_type::character_device;
  case payload_entry_type::block_device:
    return pkgimage::entry_type::block_device;
  }
  throw projection_error(projection_error_code::payload_mismatch,
                         "unknown build payload entry type");
}

void verify_entry(const payload_entry& expected,
                  const pkgimage::package_entry& observed)
{
  if (expected.path().string() != observed.path.string() ||
      image_type(expected.type()) != observed.type ||
      expected.mode() != observed.mode || expected.uid() != observed.uid ||
      expected.gid() != observed.gid || expected.size() != observed.size ||
      expected.modification_time().seconds != observed.mtime ||
      expected.modification_time().nanoseconds != observed.mtime_nanoseconds)
    throw projection_error(projection_error_code::payload_mismatch,
                           "artifact payload metadata differs at " +
                               expected.path().string());

  if (expected.symlink_target() != observed.symlink_target)
    throw projection_error(projection_error_code::payload_mismatch,
                           "artifact symbolic-link target differs at " +
                               expected.path().string());

  const std::optional<std::string> observed_hardlink =
      observed.hardlink_target
          ? std::optional<std::string>(observed.hardlink_target->string())
          : std::nullopt;
  const std::optional<std::string> expected_hardlink =
      expected.hardlink_target()
          ? std::optional<std::string>(expected.hardlink_target()->string())
          : std::nullopt;
  if (expected_hardlink != observed_hardlink)
    throw projection_error(projection_error_code::payload_mismatch,
                           "artifact hard-link target differs at " +
                               expected.path().string());

  const std::optional<device_number> observed_device =
      observed.device
          ? std::optional<device_number>(device_number{
                observed.device->major, observed.device->minor})
          : std::nullopt;
  if (expected.device() != observed_device)
    throw projection_error(projection_error_code::payload_mismatch,
                           "artifact device number differs at " +
                               expected.path().string());

  const std::optional<std::string> observed_content =
      observed.regular_content
          ? std::optional<std::string>(observed.regular_content->string())
          : std::nullopt;
  const std::optional<std::string> expected_content =
      expected.regular_content()
          ? std::optional<std::string>(
                "v1:sha256:" + expected.regular_content()->hex())
          : std::nullopt;
  if (expected_content != observed_content)
    throw projection_error(projection_error_code::payload_mismatch,
                           "artifact regular content differs at " +
                               expected.path().string());
}

void verify_payload(const payload_manifest& expected,
                    const pkgimage::package_image& observed)
{
  if (expected.entries().size() != observed.entries().size())
    throw projection_error(projection_error_code::payload_mismatch,
                           "artifact entry count differs from build payload");
  for (std::size_t index = 0; index < expected.entries().size(); ++index)
    verify_entry(expected.entries()[index], observed.entries()[index]);
}

pkgplan::artifact_manifest_identity manifest_identity(
    const build_result& build,
    const pkgsource::plan_adapter::candidate_projection& candidate,
    const pkgplan::artifact_identity& artifact,
    const pkgimage::inspected_package_image& image)
{
  canonical_record record;
  record.text("libpkgbuild-plan/artifact-manifest/v2");
  record.text(build.identity().hex());
  record.text(build.request().identity().hex());
  record.text(build.payload()->identity().hex());
  record.text(build.artifact_binding()->hex());
  record.text(candidate.candidate().identity().string());
  record.text(artifact.string());
  record.text(image.image().identity().string());
  record.text(image.receipt().identity().string());
  record.number(image.receipt().entry_count());
  return pkgplan::artifact_manifest_identity::from_sha256(sha256(record));
}

} // namespace

projection_error::projection_error(projection_error_code code,
                                   std::string message)
    : std::runtime_error(std::move(message)), code_(code)
{
}
projection_error_code projection_error::code() const noexcept { return code_; }

artifact_projection::artifact_projection(
    pkgbuild::build_result build,
    pkgsource::plan_adapter::candidate_projection candidate,
    pkgimage::inspected_package_image image,
    pkgplan::artifact_package_fact artifact)
    : build_(std::move(build)), candidate_(std::move(candidate)),
      image_(std::move(image)), artifact_(std::move(artifact))
{
}
const pkgbuild::build_result& artifact_projection::build() const noexcept { return build_; }
const pkgsource::plan_adapter::candidate_projection& artifact_projection::candidate() const noexcept { return candidate_; }
const pkgimage::inspected_package_image& artifact_projection::image() const noexcept { return image_; }
const pkgplan::artifact_package_fact& artifact_projection::artifact() const noexcept { return artifact_; }

artifact_projection project_artifact(
    pkgbuild::build_result build,
    const std::filesystem::path& artifact_path,
    const pkgimage::archive_backend& archives)
{
  if (build.outcome() != build_outcome::succeeded || !build.payload() ||
      !build.artifact() || !build.artifact_binding())
    throw projection_error(projection_error_code::build_result,
                           "planner projection requires a complete successful build result");
  if (artifact_path.empty())
    throw projection_error(projection_error_code::build_result,
                           "artifact transport pathname is empty");

  std::error_code filesystem_error;
  const auto bytes = std::filesystem::file_size(artifact_path, filesystem_error);
  if (filesystem_error || bytes != build.artifact()->byte_count())
    throw projection_error(projection_error_code::build_result,
                           "artifact transport byte count differs from build authority");

  pkgsource::plan_adapter::candidate_projection candidate = [&] {
    try {
      return pkgsource::plan_adapter::project_candidate(build.request().source());
    } catch (const pkgsource::plan_adapter::projection_error& failure) {
      throw projection_error(projection_error_code::source_projection,
                             std::string("cannot project source candidate: ") +
                                 failure.what());
    }
  }();

  pkgimage::inspected_package_image image = [&] {
    try {
      return archives.inspect(pkgimage::archive_inspection_request{
          artifact_path,
          pkgimage::complete_archive_digest::parse(
              "v1:sha256:" + build.artifact()->complete_digest().hex())});
    } catch (const pkgimage::error& failure) {
      throw projection_error(projection_error_code::archive_inspection,
                             std::string("cannot inspect sealed artifact: ") +
                                 failure.what());
    }
  }();

  verify_payload(*build.payload(), image.image());

  try {
    const auto artifact = pkgplan::artifact_identity::parse(
        image.receipt().archive_digest().string());
    const auto manifest = manifest_identity(build, candidate, artifact, image);
    pkgplan::artifact_package_fact fact(
        artifact, manifest, candidate.candidate().release());
    return artifact_projection(std::move(build), std::move(candidate),
                               std::move(image), std::move(fact));
  } catch (const projection_error&) {
    throw;
  } catch (const std::exception& failure) {
    throw projection_error(projection_error_code::planner_fact,
                           std::string("planner rejected artifact facts: ") +
                               failure.what());
  }
}

} // namespace pkgbuild::plan_adapter
