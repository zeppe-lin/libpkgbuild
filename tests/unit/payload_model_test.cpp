// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../support/test.h"

#include <libpkgbuild/libpkgbuild.h>

#include <string>
#include <vector>

int main()
{
  using namespace pkgbuild;

  TEST_CHECK(payload_path::parse("./usr//bin/").string() == "usr/bin");
  for (const auto* invalid : {"", "/usr/bin", ".", "../etc/passwd", "usr/../bin"}) {
    TEST_PKGBUILD_THROWS(error_code::invalid_model,
                         payload_path::parse(invalid));
  }

  const payload_time timestamp{1700000000, 123};
  const auto regular = payload_entry::regular(
      payload_path::parse("usr/bin/example"), 0755, 10, 20, 3, timestamp,
      sha256_digest(std::string(64, 'a')));
  const auto directory = payload_entry::directory(
      payload_path::parse("usr/bin"), 0755, 10, 20, timestamp);
  const auto symlink = payload_entry::symlink(
      payload_path::parse("usr/bin/link"), 0777, 10, 20, timestamp,
      "example");
  const auto hardlink = payload_entry::hardlink(
      payload_path::parse("usr/bin/hard"), 0755, 10, 20, timestamp,
      regular.path());
  const auto fifo = payload_entry::fifo(
      payload_path::parse("run/pipe"), 0644, 10, 20, timestamp);
  const auto character = payload_entry::character_device(
      payload_path::parse("dev/char"), 0600, 10, 20, timestamp, {1, 3});
  const auto block = payload_entry::block_device(
      payload_path::parse("dev/block"), 0600, 10, 20, timestamp, {8, 1});

  TEST_CHECK(regular.type() == payload_entry_type::regular);
  TEST_CHECK(regular.size() == 3);
  TEST_CHECK(regular.regular_content().has_value());
  TEST_CHECK(directory.type() == payload_entry_type::directory);
  TEST_CHECK(symlink.symlink_target() == std::optional<std::string>("example"));
  TEST_CHECK(hardlink.hardlink_target() == std::optional<payload_path>(regular.path()));
  TEST_CHECK(fifo.type() == payload_entry_type::fifo);
  TEST_CHECK(character.device() == std::optional<device_number>({1, 3}));
  TEST_CHECK(block.device() == std::optional<device_number>({8, 1}));

  const auto manifest = payload_manifest::seal(
      {directory, regular, hardlink, symlink, fifo, character, block});
  TEST_CHECK(manifest.entries().size() == 7);
  const auto reordered = payload_manifest::seal(
      {regular, directory, hardlink, symlink, fifo, character, block});
  TEST_CHECK(manifest.identity() != reordered.identity());

  TEST_PKGBUILD_THROWS(error_code::invalid_model,
                       payload_manifest::seal({regular, regular}));
  TEST_PKGBUILD_THROWS(error_code::invalid_model,
                       payload_manifest::seal({hardlink, regular}));
  const auto contradictory_hardlink = payload_entry::hardlink(
      payload_path::parse("usr/bin/contradictory"), 0644, 10, 20, timestamp,
      regular.path());
  TEST_PKGBUILD_THROWS(error_code::invalid_model,
                       payload_manifest::seal({regular, contradictory_hardlink}));
  TEST_PKGBUILD_THROWS(error_code::invalid_model,
      payload_entry::regular(payload_path::parse("bad/mode"), 010000, 0, 0, 0,
                             timestamp, sha256_digest(std::string(64, 'b'))));
  TEST_PKGBUILD_THROWS(error_code::invalid_model,
      payload_entry::directory(payload_path::parse("bad/time"), 0755, 0, 0,
                               payload_time{0, 1000000000U}));
  TEST_PKGBUILD_THROWS(error_code::invalid_model,
      payload_entry::symlink(payload_path::parse("bad/link"), 0777, 0, 0,
                             timestamp, "bad\nlink"));

  const auto artifact = sealed_artifact::make(
      artifact_encoding::package_tar, artifact_compression::zstd, 4096,
      sha256_digest(std::string(64, 'c')));
  const auto changed_compression = sealed_artifact::make(
      artifact_encoding::package_tar, artifact_compression::xz, 4096,
      sha256_digest(std::string(64, 'c')));
  const auto changed_size = sealed_artifact::make(
      artifact_encoding::package_tar, artifact_compression::zstd, 4097,
      sha256_digest(std::string(64, 'c')));
  TEST_CHECK(artifact.byte_count() == 4096);
  TEST_CHECK(artifact.identity() != changed_compression.identity());
  TEST_CHECK(artifact.identity() != changed_size.identity());
}
