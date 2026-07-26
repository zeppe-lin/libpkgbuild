// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file model.h
 *  \brief Parser- and executor-neutral native build values.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <libpkgbuild/identity.h>
#include <libpkgsource/model.h>
#include <libpkgsource/snapshot.h>

namespace pkgbuild {

enum class input_scope { build, check };
enum class locale_policy { c_utf8 };
enum class timezone_policy { utc };
enum class network_policy { denied };
enum class home_policy { isolated };
enum class output_layout_kind { package_root_v1 };
enum class payload_entry_type {
  regular,
  directory,
  symlink,
  hardlink,
  fifo,
  character_device,
  block_device,
};
enum class artifact_encoding { package_tar_v1 };
enum class artifact_compression { none, gzip, xz, zstd };
enum class build_outcome { succeeded, failed };

[[nodiscard]] std::string_view to_string(input_scope value) noexcept;
[[nodiscard]] std::string_view to_string(locale_policy value) noexcept;
[[nodiscard]] std::string_view to_string(timezone_policy value) noexcept;
[[nodiscard]] std::string_view to_string(network_policy value) noexcept;
[[nodiscard]] std::string_view to_string(home_policy value) noexcept;
[[nodiscard]] std::string_view to_string(output_layout_kind value) noexcept;
[[nodiscard]] std::string_view to_string(payload_entry_type value) noexcept;
[[nodiscard]] std::string_view to_string(artifact_encoding value) noexcept;
[[nodiscard]] std::string_view to_string(artifact_compression value) noexcept;
[[nodiscard]] std::string_view to_string(build_outcome value) noexcept;

/*! \brief Canonical exact-byte SHA-256 value. */
class sha256_digest final {
public:
  explicit sha256_digest(std::string hex);
  [[nodiscard]] const std::string& hex() const noexcept;
  friend bool operator==(const sha256_digest& lhs,
                         const sha256_digest& rhs) noexcept;
  friend bool operator!=(const sha256_digest& lhs,
                         const sha256_digest& rhs) noexcept;
  friend bool operator<(const sha256_digest& lhs,
                        const sha256_digest& rhs) noexcept;
private:
  std::string hex_;
};

/*! \brief One source declaration bound to verified material bytes. */
class materialized_source final {
public:
  [[nodiscard]] static materialized_source verify(
      pkgsource::source_input declaration,
      sha256_digest observed_content);
  [[nodiscard]] const pkgsource::source_input& declaration() const noexcept;
  [[nodiscard]] const sha256_digest& observed_content() const noexcept;
  [[nodiscard]] const source_material_identity& identity() const noexcept;
  friend bool operator==(const materialized_source& lhs,
                         const materialized_source& rhs) noexcept;
  friend bool operator!=(const materialized_source& lhs,
                         const materialized_source& rhs) noexcept;
  friend bool operator<(const materialized_source& lhs,
                        const materialized_source& rhs) noexcept;
private:
  materialized_source(pkgsource::source_input declaration,
                      sha256_digest observed_content,
                      source_material_identity identity);
  pkgsource::source_input declaration_;
  sha256_digest observed_content_;
  source_material_identity identity_;
};

/*! \brief One exact package requirement resolved to a concrete build input. */
class resolved_package_input final {
public:
  [[nodiscard]] static resolved_package_input make(
      input_scope scope,
      pkgsource::package_reference declared_package,
      pkgsource::package_release resolved_release,
      pkgsource::source_snapshot_identity source_snapshot,
      build_result_identity build_result,
      artifact_identity artifact);
  [[nodiscard]] input_scope scope() const noexcept;
  [[nodiscard]] const pkgsource::package_reference& declared_package() const noexcept;
  [[nodiscard]] const pkgsource::package_release& resolved_release() const noexcept;
  [[nodiscard]] const pkgsource::source_snapshot_identity& source_snapshot() const noexcept;
  [[nodiscard]] const build_result_identity& build_result() const noexcept;
  [[nodiscard]] const artifact_identity& artifact() const noexcept;
  [[nodiscard]] const resolved_package_input_identity& identity() const noexcept;
  friend bool operator==(const resolved_package_input& lhs,
                         const resolved_package_input& rhs) noexcept;
  friend bool operator!=(const resolved_package_input& lhs,
                         const resolved_package_input& rhs) noexcept;
  friend bool operator<(const resolved_package_input& lhs,
                        const resolved_package_input& rhs) noexcept;
private:
  resolved_package_input(input_scope scope,
                         pkgsource::package_reference declared_package,
                         pkgsource::package_release resolved_release,
                         pkgsource::source_snapshot_identity source_snapshot,
                         build_result_identity build_result,
                         artifact_identity artifact,
                         resolved_package_input_identity identity);
  input_scope scope_;
  pkgsource::package_reference declared_package_;
  pkgsource::package_release resolved_release_;
  pkgsource::source_snapshot_identity source_snapshot_;
  build_result_identity build_result_;
  artifact_identity artifact_;
  resolved_package_input_identity identity_;
};

/*! \brief Concrete filesystem tree made available for one resolved input. */
class materialized_package_input final {
public:
  materialized_package_input(resolved_package_input resolved,
                             input_tree_identity tree);
  [[nodiscard]] const resolved_package_input& resolved() const noexcept;
  [[nodiscard]] const input_tree_identity& tree() const noexcept;
  friend bool operator==(const materialized_package_input& lhs,
                         const materialized_package_input& rhs) noexcept;
  friend bool operator!=(const materialized_package_input& lhs,
                         const materialized_package_input& rhs) noexcept;
  friend bool operator<(const materialized_package_input& lhs,
                        const materialized_package_input& rhs) noexcept;
private:
  resolved_package_input resolved_;
  input_tree_identity tree_;
};

/*! \brief Closed process-environment policy for one build. */
class environment_policy final {
public:
  [[nodiscard]] static environment_policy hermetic(
      std::uint32_t parallelism,
      std::uint32_t file_creation_mask = 0022,
      std::optional<std::int64_t> source_date_epoch = std::nullopt);
  [[nodiscard]] locale_policy locale() const noexcept;
  [[nodiscard]] timezone_policy timezone() const noexcept;
  [[nodiscard]] network_policy network() const noexcept;
  [[nodiscard]] home_policy home() const noexcept;
  [[nodiscard]] std::uint32_t parallelism() const noexcept;
  [[nodiscard]] std::uint32_t file_creation_mask() const noexcept;
  [[nodiscard]] const std::optional<std::int64_t>& source_date_epoch() const noexcept;
  [[nodiscard]] const environment_policy_identity& identity() const noexcept;
  friend bool operator==(const environment_policy& lhs,
                         const environment_policy& rhs) noexcept;
  friend bool operator!=(const environment_policy& lhs,
                         const environment_policy& rhs) noexcept;
  friend bool operator<(const environment_policy& lhs,
                        const environment_policy& rhs) noexcept;
private:
  environment_policy(std::uint32_t parallelism,
                     std::uint32_t file_creation_mask,
                     std::optional<std::int64_t> source_date_epoch,
                     environment_policy_identity identity);
  std::uint32_t parallelism_;
  std::uint32_t file_creation_mask_;
  std::optional<std::int64_t> source_date_epoch_;
  environment_policy_identity identity_;
};

/*! \brief Canonical path inside an intended package payload. */
class payload_path final {
public:
  [[nodiscard]] static payload_path parse(std::string_view input);
  [[nodiscard]] const std::string& string() const noexcept;
  friend bool operator==(const payload_path& lhs,
                         const payload_path& rhs) noexcept;
  friend bool operator!=(const payload_path& lhs,
                         const payload_path& rhs) noexcept;
  friend bool operator<(const payload_path& lhs,
                        const payload_path& rhs) noexcept;
private:
  explicit payload_path(std::string value);
  std::string value_;
};

struct payload_time final {
  std::int64_t seconds = 0;
  std::uint32_t nanoseconds = 0;
};
[[nodiscard]] bool operator==(const payload_time& lhs,
                              const payload_time& rhs) noexcept;
[[nodiscard]] bool operator!=(const payload_time& lhs,
                              const payload_time& rhs) noexcept;
[[nodiscard]] bool operator<(const payload_time& lhs,
                             const payload_time& rhs) noexcept;

struct device_number final {
  std::uint64_t major = 0;
  std::uint64_t minor = 0;
};
[[nodiscard]] bool operator==(const device_number& lhs,
                              const device_number& rhs) noexcept;
[[nodiscard]] bool operator!=(const device_number& lhs,
                              const device_number& rhs) noexcept;
[[nodiscard]] bool operator<(const device_number& lhs,
                             const device_number& rhs) noexcept;

/*! \brief One fully described intended payload object. */
class payload_entry final {
public:
  [[nodiscard]] static payload_entry regular(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, std::uint64_t size, payload_time modification_time,
      sha256_digest content);
  [[nodiscard]] static payload_entry directory(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, payload_time modification_time);
  [[nodiscard]] static payload_entry symlink(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, payload_time modification_time,
      std::string target);
  [[nodiscard]] static payload_entry hardlink(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, payload_time modification_time,
      payload_path target);
  [[nodiscard]] static payload_entry fifo(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, payload_time modification_time);
  [[nodiscard]] static payload_entry character_device(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, payload_time modification_time,
      device_number device);
  [[nodiscard]] static payload_entry block_device(
      payload_path path, std::uint32_t mode, std::uint64_t uid,
      std::uint64_t gid, payload_time modification_time,
      device_number device);

  [[nodiscard]] const payload_path& path() const noexcept;
  [[nodiscard]] payload_entry_type type() const noexcept;
  [[nodiscard]] std::uint32_t mode() const noexcept;
  [[nodiscard]] std::uint64_t uid() const noexcept;
  [[nodiscard]] std::uint64_t gid() const noexcept;
  [[nodiscard]] std::uint64_t size() const noexcept;
  [[nodiscard]] const payload_time& modification_time() const noexcept;
  [[nodiscard]] const std::optional<std::string>& symlink_target() const noexcept;
  [[nodiscard]] const std::optional<payload_path>& hardlink_target() const noexcept;
  [[nodiscard]] const std::optional<device_number>& device() const noexcept;
  [[nodiscard]] const std::optional<sha256_digest>& regular_content() const noexcept;
  friend bool operator==(const payload_entry& lhs,
                         const payload_entry& rhs) noexcept;
  friend bool operator!=(const payload_entry& lhs,
                         const payload_entry& rhs) noexcept;
private:
  payload_entry(payload_path path, payload_entry_type type,
                std::uint32_t mode, std::uint64_t uid, std::uint64_t gid,
                std::uint64_t size, payload_time modification_time,
                std::optional<std::string> symlink_target,
                std::optional<payload_path> hardlink_target,
                std::optional<device_number> device,
                std::optional<sha256_digest> regular_content);
  payload_path path_;
  payload_entry_type type_;
  std::uint32_t mode_;
  std::uint64_t uid_;
  std::uint64_t gid_;
  std::uint64_t size_;
  payload_time modification_time_;
  std::optional<std::string> symlink_target_;
  std::optional<payload_path> hardlink_target_;
  std::optional<device_number> device_;
  std::optional<sha256_digest> regular_content_;
};

/*! \brief Ordered complete intended package payload. */
class payload_manifest final {
public:
  [[nodiscard]] static payload_manifest seal(std::vector<payload_entry> entries);
  [[nodiscard]] const std::vector<payload_entry>& entries() const noexcept;
  [[nodiscard]] const payload_manifest_identity& identity() const noexcept;
  friend bool operator==(const payload_manifest& lhs,
                         const payload_manifest& rhs) noexcept;
  friend bool operator!=(const payload_manifest& lhs,
                         const payload_manifest& rhs) noexcept;
private:
  payload_manifest(std::vector<payload_entry> entries,
                   payload_manifest_identity identity);
  std::vector<payload_entry> entries_;
  payload_manifest_identity identity_;
};

/*! \brief Exact retained package artifact bytes and encoding. */
class sealed_artifact final {
public:
  [[nodiscard]] static sealed_artifact make(
      artifact_encoding encoding,
      artifact_compression compression,
      std::uint64_t byte_count,
      sha256_digest complete_digest);
  [[nodiscard]] artifact_encoding encoding() const noexcept;
  [[nodiscard]] artifact_compression compression() const noexcept;
  [[nodiscard]] std::uint64_t byte_count() const noexcept;
  [[nodiscard]] const sha256_digest& complete_digest() const noexcept;
  [[nodiscard]] const artifact_identity& identity() const noexcept;
  friend bool operator==(const sealed_artifact& lhs,
                         const sealed_artifact& rhs) noexcept;
  friend bool operator!=(const sealed_artifact& lhs,
                         const sealed_artifact& rhs) noexcept;
private:
  sealed_artifact(artifact_encoding encoding,
                  artifact_compression compression,
                  std::uint64_t byte_count,
                  sha256_digest complete_digest,
                  artifact_identity identity);
  artifact_encoding encoding_;
  artifact_compression compression_;
  std::uint64_t byte_count_;
  sha256_digest complete_digest_;
  artifact_identity identity_;
};

} // namespace pkgbuild
