// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/model.h>

#include <libpkgbuild/error.h>

#include "identity_support.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>

namespace pkgbuild {
namespace {

[[noreturn]] void invalid(std::string message)
{
  throw error(error_code::invalid_model, std::move(message));
}

void require_line_safe(std::string_view value, std::string_view field)
{
  if (value.empty())
    invalid(std::string(field) + " must not be empty");
  for (char byte : value) {
    if (byte == '\0' || byte == '\n' || byte == '\r')
      invalid(std::string(field) + " contains a forbidden byte");
  }
}

environment_policy_identity environment_id(
    std::uint32_t parallelism,
    std::uint32_t file_creation_mask,
    const std::optional<std::int64_t>& source_date_epoch)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/environment-policy/1");
  writer.text(to_string(locale_policy::c_utf8));
  writer.text(to_string(timezone_policy::utc));
  writer.text(to_string(network_policy::denied));
  writer.text(to_string(home_policy::isolated));
  writer.number(parallelism);
  writer.number(file_creation_mask);
  writer.boolean(source_date_epoch.has_value());
  if (source_date_epoch)
    writer.number(static_cast<std::uint64_t>(*source_date_epoch));
  return environment_policy_identity::from_sha256(writer.finish());
}

void write_payload_entry(detail::identity_writer& writer,
                         const payload_entry& entry)
{
  writer.text(entry.path().string());
  writer.text(to_string(entry.type()));
  writer.number(entry.mode());
  writer.number(entry.uid());
  writer.number(entry.gid());
  writer.number(entry.size());
  writer.number(static_cast<std::uint64_t>(entry.modification_time().seconds));
  writer.number(entry.modification_time().nanoseconds);
  writer.boolean(entry.symlink_target().has_value());
  if (entry.symlink_target())
    writer.text(*entry.symlink_target());
  writer.boolean(entry.hardlink_target().has_value());
  if (entry.hardlink_target())
    writer.text(entry.hardlink_target()->string());
  writer.boolean(entry.device().has_value());
  if (entry.device()) {
    writer.number(entry.device()->major);
    writer.number(entry.device()->minor);
  }
  writer.boolean(entry.regular_content().has_value());
  if (entry.regular_content())
    writer.text(entry.regular_content()->hex());
}

payload_manifest_identity manifest_id(const std::vector<payload_entry>& entries)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/payload-manifest/1");
  writer.number(entries.size());
  for (const auto& entry : entries)
    write_payload_entry(writer, entry);
  return payload_manifest_identity::from_sha256(writer.finish());
}

artifact_identity artifact_id(artifact_encoding encoding,
                              artifact_compression compression,
                              std::uint64_t byte_count,
                              const sha256_digest& digest)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/artifact/1");
  writer.text(to_string(encoding));
  writer.text(to_string(compression));
  writer.number(byte_count);
  writer.text(digest.hex());
  return artifact_identity::from_sha256(writer.finish());
}

} // namespace

std::string_view to_string(input_scope value) noexcept
{
  switch (value) {
  case input_scope::build: return "build";
  case input_scope::check: return "check";
  }
  return "unknown";
}
std::string_view to_string(locale_policy value) noexcept
{ return value == locale_policy::c_utf8 ? "C.UTF-8" : "unknown"; }
std::string_view to_string(timezone_policy value) noexcept
{ return value == timezone_policy::utc ? "UTC" : "unknown"; }
std::string_view to_string(network_policy value) noexcept
{ return value == network_policy::denied ? "denied" : "unknown"; }
std::string_view to_string(home_policy value) noexcept
{ return value == home_policy::isolated ? "isolated" : "unknown"; }
std::string_view to_string(output_layout_kind value) noexcept
{ return value == output_layout_kind::package_root ? "package-root" : "unknown"; }
std::string_view to_string(payload_entry_type value) noexcept
{
  switch (value) {
  case payload_entry_type::regular: return "regular";
  case payload_entry_type::directory: return "directory";
  case payload_entry_type::symlink: return "symlink";
  case payload_entry_type::hardlink: return "hardlink";
  case payload_entry_type::fifo: return "fifo";
  case payload_entry_type::character_device: return "character-device";
  case payload_entry_type::block_device: return "block-device";
  }
  return "unknown";
}
std::string_view to_string(artifact_encoding value) noexcept
{ return value == artifact_encoding::package_tar ? "package-tar" : "unknown"; }
std::string_view to_string(artifact_compression value) noexcept
{
  switch (value) {
  case artifact_compression::none: return "none";
  case artifact_compression::gzip: return "gzip";
  case artifact_compression::xz: return "xz";
  case artifact_compression::zstd: return "zstd";
  }
  return "unknown";
}
std::string_view to_string(build_outcome value) noexcept
{ return value == build_outcome::succeeded ? "succeeded" : "failed"; }

sha256_digest::sha256_digest(std::string hex) : hex_(std::move(hex))
{ detail::require_sha256_hex(hex_); }
const std::string& sha256_digest::hex() const noexcept { return hex_; }
bool operator==(const sha256_digest& lhs, const sha256_digest& rhs) noexcept
{ return lhs.hex_ == rhs.hex_; }
bool operator!=(const sha256_digest& lhs, const sha256_digest& rhs) noexcept
{ return !(lhs == rhs); }
bool operator<(const sha256_digest& lhs, const sha256_digest& rhs) noexcept
{ return lhs.hex_ < rhs.hex_; }

environment_policy::environment_policy(
    std::uint32_t parallelism, std::uint32_t file_creation_mask,
    std::optional<std::int64_t> source_date_epoch,
    environment_policy_identity identity)
    : parallelism_(parallelism), file_creation_mask_(file_creation_mask),
      source_date_epoch_(source_date_epoch), identity_(std::move(identity))
{
}

environment_policy environment_policy::hermetic(
    std::uint32_t parallelism, std::uint32_t file_creation_mask,
    std::optional<std::int64_t> source_date_epoch)
{
  if (parallelism == 0)
    invalid("build parallelism must be greater than zero");
  if (file_creation_mask > 0777)
    invalid("file creation mask exceeds POSIX permission bits");
  if (source_date_epoch && *source_date_epoch < 0)
    invalid("SOURCE_DATE_EPOCH must not be negative");
  auto identity = environment_id(parallelism, file_creation_mask,
                                 source_date_epoch);
  return environment_policy(parallelism, file_creation_mask,
                            source_date_epoch, std::move(identity));
}

locale_policy environment_policy::locale() const noexcept { return locale_policy::c_utf8; }
timezone_policy environment_policy::timezone() const noexcept { return timezone_policy::utc; }
network_policy environment_policy::network() const noexcept { return network_policy::denied; }
home_policy environment_policy::home() const noexcept { return home_policy::isolated; }
std::uint32_t environment_policy::parallelism() const noexcept { return parallelism_; }
std::uint32_t environment_policy::file_creation_mask() const noexcept { return file_creation_mask_; }
const std::optional<std::int64_t>& environment_policy::source_date_epoch() const noexcept { return source_date_epoch_; }
const environment_policy_identity& environment_policy::identity() const noexcept { return identity_; }
bool operator==(const environment_policy& lhs, const environment_policy& rhs) noexcept { return lhs.identity_ == rhs.identity_; }
bool operator!=(const environment_policy& lhs, const environment_policy& rhs) noexcept { return !(lhs == rhs); }
bool operator<(const environment_policy& lhs, const environment_policy& rhs) noexcept { return lhs.identity_ < rhs.identity_; }

payload_path::payload_path(std::string value) : value_(std::move(value)) {}
payload_path payload_path::parse(std::string_view input)
{
  if (input.empty() || input.front() == '/')
    invalid("payload path must be non-empty and relative");
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= input.size()) {
    const auto end = input.find('/', begin);
    const auto length = (end == std::string_view::npos ? input.size() : end) - begin;
    const auto part = input.substr(begin, length);
    for (char byte : part) {
      if (byte == '\0' || byte == '\n' || byte == '\r')
        invalid("payload path contains a forbidden byte");
    }
    if (part == "..")
      invalid("payload path escapes the package root");
    if (!part.empty() && part != ".")
      parts.emplace_back(part);
    if (end == std::string_view::npos)
      break;
    begin = end + 1;
  }
  if (parts.empty())
    invalid("payload path normalizes to an empty value");
  std::string normalized;
  for (const auto& part : parts) {
    if (!normalized.empty())
      normalized.push_back('/');
    normalized += part;
  }
  return payload_path(std::move(normalized));
}
const std::string& payload_path::string() const noexcept { return value_; }
bool operator==(const payload_path& lhs, const payload_path& rhs) noexcept { return lhs.value_ == rhs.value_; }
bool operator!=(const payload_path& lhs, const payload_path& rhs) noexcept { return !(lhs == rhs); }
bool operator<(const payload_path& lhs, const payload_path& rhs) noexcept { return lhs.value_ < rhs.value_; }

bool operator==(const payload_time& lhs, const payload_time& rhs) noexcept { return lhs.seconds == rhs.seconds && lhs.nanoseconds == rhs.nanoseconds; }
bool operator!=(const payload_time& lhs, const payload_time& rhs) noexcept { return !(lhs == rhs); }
bool operator<(const payload_time& lhs, const payload_time& rhs) noexcept { return std::tie(lhs.seconds, lhs.nanoseconds) < std::tie(rhs.seconds, rhs.nanoseconds); }
bool operator==(const device_number& lhs, const device_number& rhs) noexcept { return lhs.major == rhs.major && lhs.minor == rhs.minor; }
bool operator!=(const device_number& lhs, const device_number& rhs) noexcept { return !(lhs == rhs); }
bool operator<(const device_number& lhs, const device_number& rhs) noexcept { return std::tie(lhs.major, lhs.minor) < std::tie(rhs.major, rhs.minor); }

payload_entry::payload_entry(
    payload_path path, payload_entry_type type, std::uint32_t mode,
    std::uint64_t uid, std::uint64_t gid, std::uint64_t size,
    payload_time modification_time, std::optional<std::string> symlink_target,
    std::optional<payload_path> hardlink_target,
    std::optional<device_number> device,
    std::optional<sha256_digest> regular_content)
    : path_(std::move(path)), type_(type), mode_(mode), uid_(uid), gid_(gid),
      size_(size), modification_time_(modification_time),
      symlink_target_(std::move(symlink_target)),
      hardlink_target_(std::move(hardlink_target)), device_(device),
      regular_content_(std::move(regular_content))
{
  if (mode_ > 07777)
    invalid("payload mode exceeds POSIX permission and special bits");
  if (modification_time_.nanoseconds >= 1000000000U)
    invalid("payload timestamp nanoseconds are out of range");
}

payload_entry payload_entry::regular(payload_path path, std::uint32_t mode,
    std::uint64_t uid, std::uint64_t gid, std::uint64_t size,
    payload_time modification_time, sha256_digest content)
{
  return payload_entry(std::move(path), payload_entry_type::regular, mode, uid,
                       gid, size, modification_time, std::nullopt, std::nullopt,
                       std::nullopt, std::move(content));
}
payload_entry payload_entry::directory(payload_path path, std::uint32_t mode,
    std::uint64_t uid, std::uint64_t gid, payload_time modification_time)
{
  return payload_entry(std::move(path), payload_entry_type::directory, mode,
                       uid, gid, 0, modification_time, std::nullopt,
                       std::nullopt, std::nullopt, std::nullopt);
}
payload_entry payload_entry::symlink(payload_path path, std::uint32_t mode,
    std::uint64_t uid, std::uint64_t gid, payload_time modification_time,
    std::string target)
{
  require_line_safe(target, "symbolic-link target");
  return payload_entry(std::move(path), payload_entry_type::symlink, mode, uid,
                       gid, 0, modification_time, std::move(target),
                       std::nullopt, std::nullopt, std::nullopt);
}
payload_entry payload_entry::hardlink(payload_path path, std::uint32_t mode,
    std::uint64_t uid, std::uint64_t gid, payload_time modification_time,
    payload_path target)
{
  return payload_entry(std::move(path), payload_entry_type::hardlink, mode, uid,
                       gid, 0, modification_time, std::nullopt,
                       std::move(target), std::nullopt, std::nullopt);
}
payload_entry payload_entry::fifo(payload_path path, std::uint32_t mode,
    std::uint64_t uid, std::uint64_t gid, payload_time modification_time)
{
  return payload_entry(std::move(path), payload_entry_type::fifo, mode, uid,
                       gid, 0, modification_time, std::nullopt, std::nullopt,
                       std::nullopt, std::nullopt);
}
payload_entry payload_entry::character_device(payload_path path,
    std::uint32_t mode, std::uint64_t uid, std::uint64_t gid,
    payload_time modification_time, device_number device)
{
  return payload_entry(std::move(path), payload_entry_type::character_device,
                       mode, uid, gid, 0, modification_time, std::nullopt,
                       std::nullopt, device, std::nullopt);
}
payload_entry payload_entry::block_device(payload_path path,
    std::uint32_t mode, std::uint64_t uid, std::uint64_t gid,
    payload_time modification_time, device_number device)
{
  return payload_entry(std::move(path), payload_entry_type::block_device,
                       mode, uid, gid, 0, modification_time, std::nullopt,
                       std::nullopt, device, std::nullopt);
}

const payload_path& payload_entry::path() const noexcept { return path_; }
payload_entry_type payload_entry::type() const noexcept { return type_; }
std::uint32_t payload_entry::mode() const noexcept { return mode_; }
std::uint64_t payload_entry::uid() const noexcept { return uid_; }
std::uint64_t payload_entry::gid() const noexcept { return gid_; }
std::uint64_t payload_entry::size() const noexcept { return size_; }
const payload_time& payload_entry::modification_time() const noexcept { return modification_time_; }
const std::optional<std::string>& payload_entry::symlink_target() const noexcept { return symlink_target_; }
const std::optional<payload_path>& payload_entry::hardlink_target() const noexcept { return hardlink_target_; }
const std::optional<device_number>& payload_entry::device() const noexcept { return device_; }
const std::optional<sha256_digest>& payload_entry::regular_content() const noexcept { return regular_content_; }
bool operator==(const payload_entry& lhs, const payload_entry& rhs) noexcept
{
  return lhs.path_ == rhs.path_ && lhs.type_ == rhs.type_ &&
      lhs.mode_ == rhs.mode_ && lhs.uid_ == rhs.uid_ && lhs.gid_ == rhs.gid_ &&
      lhs.size_ == rhs.size_ && lhs.modification_time_ == rhs.modification_time_ &&
      lhs.symlink_target_ == rhs.symlink_target_ &&
      lhs.hardlink_target_ == rhs.hardlink_target_ && lhs.device_ == rhs.device_ &&
      lhs.regular_content_ == rhs.regular_content_;
}
bool operator!=(const payload_entry& lhs, const payload_entry& rhs) noexcept { return !(lhs == rhs); }

payload_manifest::payload_manifest(std::vector<payload_entry> entries,
                                   payload_manifest_identity identity)
    : entries_(std::move(entries)), identity_(std::move(identity))
{
}

payload_manifest payload_manifest::seal(std::vector<payload_entry> entries)
{
  std::set<payload_path> seen;
  std::map<payload_path, const payload_entry*> regular;
  for (const auto& entry : entries) {
    if (!seen.insert(entry.path()).second)
      invalid("payload manifest contains a duplicate path: " + entry.path().string());
    if (entry.type() == payload_entry_type::regular)
      regular.emplace(entry.path(), &entry);
    if (entry.type() == payload_entry_type::hardlink) {
      if (!entry.hardlink_target())
        invalid("hard-link entry lacks its regular payload anchor");
      if (entry.path() == *entry.hardlink_target())
        invalid("hard-link entry cannot target itself");
      const auto anchor = regular.find(*entry.hardlink_target());
      if (anchor == regular.end())
        invalid("hard-link target must name an earlier regular payload entry");
      if (entry.mode() != anchor->second->mode() ||
          entry.uid() != anchor->second->uid() ||
          entry.gid() != anchor->second->gid() ||
          entry.modification_time() != anchor->second->modification_time())
        invalid("hard-link metadata differs from its regular payload anchor");
    }
  }
  auto identity = manifest_id(entries);
  return payload_manifest(std::move(entries), std::move(identity));
}
const std::vector<payload_entry>& payload_manifest::entries() const noexcept { return entries_; }
const payload_manifest_identity& payload_manifest::identity() const noexcept { return identity_; }
bool operator==(const payload_manifest& lhs, const payload_manifest& rhs) noexcept { return lhs.identity_ == rhs.identity_ && lhs.entries_ == rhs.entries_; }
bool operator!=(const payload_manifest& lhs, const payload_manifest& rhs) noexcept { return !(lhs == rhs); }

sealed_artifact::sealed_artifact(artifact_encoding encoding,
    artifact_compression compression, std::uint64_t byte_count,
    sha256_digest complete_digest, artifact_identity identity)
    : encoding_(encoding), compression_(compression), byte_count_(byte_count),
      complete_digest_(std::move(complete_digest)), identity_(std::move(identity))
{
}
sealed_artifact sealed_artifact::make(artifact_encoding encoding,
    artifact_compression compression, std::uint64_t byte_count,
    sha256_digest complete_digest)
{
  auto identity = artifact_id(encoding, compression, byte_count, complete_digest);
  return sealed_artifact(encoding, compression, byte_count,
                         std::move(complete_digest), std::move(identity));
}
artifact_encoding sealed_artifact::encoding() const noexcept { return encoding_; }
artifact_compression sealed_artifact::compression() const noexcept { return compression_; }
std::uint64_t sealed_artifact::byte_count() const noexcept { return byte_count_; }
const sha256_digest& sealed_artifact::complete_digest() const noexcept { return complete_digest_; }
const artifact_identity& sealed_artifact::identity() const noexcept { return identity_; }
bool operator==(const sealed_artifact& lhs, const sealed_artifact& rhs) noexcept { return lhs.identity_ == rhs.identity_; }
bool operator!=(const sealed_artifact& lhs, const sealed_artifact& rhs) noexcept { return !(lhs == rhs); }

} // namespace pkgbuild
