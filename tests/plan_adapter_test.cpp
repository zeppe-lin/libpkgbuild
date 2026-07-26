// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.h"

#include <libpkgbuild-plan/adapter.h>
#include <libpkgimage/libarchive_backend.h>

#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

class temporary_directory final {
public:
  temporary_directory()
  {
    path_ = std::filesystem::temp_directory_path() /
        ("libpkgbuild-plan-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~temporary_directory() { std::filesystem::remove_all(path_); }
  const std::filesystem::path& path() const noexcept { return path_; }
private:
  std::filesystem::path path_;
};

void check_archive(int status, archive* value)
{
  if (status != ARCHIVE_OK)
    throw std::runtime_error(archive_error_string(value));
}

void add_entry(archive* output, const char* path, mode_t type, mode_t mode,
               std::int64_t mtime, const char* payload = nullptr,
               const char* link = nullptr)
{
  archive_entry* raw = archive_entry_new();
  if (!raw)
    throw std::bad_alloc();
  struct holder final {
    archive_entry* value;
    ~holder() { archive_entry_free(value); }
  } entry{raw};
  archive_entry_set_pathname(entry.value, path);
  archive_entry_set_filetype(entry.value, type);
  archive_entry_set_perm(entry.value, mode);
  archive_entry_set_uid(entry.value, 0);
  archive_entry_set_gid(entry.value, 0);
  archive_entry_set_mtime(entry.value, mtime, 0);
  if (type == AE_IFREG) {
    archive_entry_set_size(entry.value, 3);
  } else {
    archive_entry_set_size(entry.value, 0);
  }
  if (type == AE_IFLNK)
    archive_entry_set_symlink(entry.value, link);
  if (link && type == AE_IFREG)
    archive_entry_set_hardlink(entry.value, link);
  check_archive(archive_write_header(output, entry.value), output);
  if (payload && archive_write_data(output, payload, 3) != 3)
    throw std::runtime_error("cannot write archive payload");
}

std::filesystem::path make_archive(const std::filesystem::path& directory)
{
  const auto path = directory / "example.pkg.tar";
  archive* raw = archive_write_new();
  if (!raw)
    throw std::bad_alloc();
  struct holder final {
    archive* value;
    ~holder() { archive_write_free(value); }
  } output{raw};
  check_archive(archive_write_set_format_pax_restricted(output.value), output.value);
  check_archive(archive_write_open_filename(output.value, path.c_str()), output.value);
  add_entry(output.value, "usr/bin", AE_IFDIR, 0755, 1700000000);
  add_entry(output.value, "usr/bin/example", AE_IFREG, 0755, 1700000000, "abc");
  add_entry(output.value, "usr/bin/example-link", AE_IFREG, 0755,
            1700000000, nullptr, "usr/bin/example");
  add_entry(output.value, "usr/bin/example-symlink", AE_IFLNK, 0777,
            1700000000, nullptr, "example");
  check_archive(archive_write_close(output.value), output.value);
  return path;
}

std::string sha256_file(const std::filesystem::path& path)
{
  EVP_MD_CTX* raw = EVP_MD_CTX_new();
  if (!raw)
    throw std::bad_alloc();
  struct holder final {
    EVP_MD_CTX* value;
    ~holder() { EVP_MD_CTX_free(value); }
  } context{raw};
  if (EVP_DigestInit_ex(context.value, EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("cannot initialize digest");
  std::ifstream input(path, std::ios::binary);
  std::array<char, 4096> block{};
  while (input) {
    input.read(block.data(), block.size());
    if (input.gcount() > 0 &&
        EVP_DigestUpdate(context.value, block.data(), input.gcount()) != 1)
      throw std::runtime_error("cannot update digest");
  }
  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned int size = 0;
  if (EVP_DigestFinal_ex(context.value, bytes.data(), &size) != 1 || size != 32)
    throw std::runtime_error("cannot finalize digest");
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned int index = 0; index < size; ++index)
    out << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  return out.str();
}

pkgbuild::payload_manifest matching_payload(std::uint32_t regular_mode = 0755)
{
  using namespace pkgbuild;
  return payload_manifest::seal({
      payload_entry::directory(payload_path::parse("usr/bin"), 0755, 0, 0,
                               payload_time{1700000000, 0}),
      payload_entry::regular(
          payload_path::parse("usr/bin/example"), regular_mode, 0, 0, 3,
          payload_time{1700000000, 0},
          sha256_digest(
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")),
      payload_entry::hardlink(payload_path::parse("usr/bin/example-link"),
                              0755, 0, 0,
                              payload_time{1700000000, 0},
                              payload_path::parse("usr/bin/example")),
      payload_entry::symlink(payload_path::parse("usr/bin/example-symlink"),
                             0777, 0, 0,
                             payload_time{1700000000, 0}, "example"),
  });
}

pkgbuild::build_result result(const std::filesystem::path& path,
                              pkgbuild::payload_manifest payload)
{
  return pkgbuild::build_result::succeeded(
      fixture::request(), std::move(payload),
      pkgbuild::sealed_artifact::make(
          pkgbuild::artifact_encoding::package_tar_v1,
          pkgbuild::artifact_compression::none,
          std::filesystem::file_size(path),
          pkgbuild::sha256_digest(sha256_file(path))),
      pkgbuild::execution_evidence_identity::from_sha256(std::string(64, '8')));
}

} // namespace

int main()
{
  temporary_directory temporary;
  const auto path = make_archive(temporary.path());
  pkgimage::libarchive_backend backend;
  const auto projected = pkgbuild::plan_adapter::project_artifact(
      result(path, matching_payload()), path, backend);
  assert(projected.build().outcome() == pkgbuild::build_outcome::succeeded);
  assert(projected.image().image().entries().size() == 4);
  assert(projected.artifact().release().name() == "example");
  assert(projected.artifact().artifact().string() ==
         projected.image().receipt().archive_digest().string());

  try {
    (void)pkgbuild::plan_adapter::project_artifact(
        result(path, matching_payload(0644)), path, backend);
    assert(false);
  } catch (const pkgbuild::plan_adapter::projection_error& failure) {
    assert(failure.code() ==
           pkgbuild::plan_adapter::projection_error_code::payload_mismatch);
  }
}
