// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <libpkgbuild/libpkgbuild.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace fixture {

inline pkgsource::declaration_provenance at(const char* document,
                                            const char* path,
                                            std::uint32_t line)
{
  return pkgsource::declaration_provenance(document, path, line, 3);
}

inline pkgsource::profile_catalog profiles()
{
  using namespace pkgsource;
  return profile_catalog::seal({
      profile_declaration(
          profile_reference("@toolchain"), at("profiles.yml", "toolchain", 1),
          {
              profile_member_declaration(
                  requirement_subject(package_reference("binutils")),
                  at("profiles.yml", "toolchain[0]", 2)),
              profile_member_declaration(
                  requirement_subject(package_reference("gcc")),
                  at("profiles.yml", "toolchain[1]", 3)),
          }),
  });
}

inline pkgsource::source_snapshot source()
{
  using namespace pkgsource;
  return seal_source(
      source_origin("recipe.yml"), source_syntax::recipe_yaml_v1,
      recipe_declaration(
          package_release(package_reference("example"), "1.2.3", 1),
          package_metadata("Example", std::nullopt,
                           "https://example.invalid", {"MIT"}),
          {
              source_input::remote(
                  "https://example.invalid/example.tar.xz", "example.tar.xz",
                  digest(digest_algorithm::sha256, std::string(64, 'a'))),
              source_input::local(
                  "files/example.conf", "example.conf",
                  digest(digest_algorithm::sha256, std::string(64, 'b'))),
          },
          program(program_language::posix_shell,
                  "meson setup build\nmeson compile -C build\n"),
          {
              requirement_declaration(
                  requirement_scope::build(),
                  requirement_subject(profile_reference("@toolchain")),
                  at("recipe.yml", "requirements.build[0]", 12)),
              requirement_declaration(
                  requirement_scope::check(),
                  requirement_subject(package_reference("pkgcheck")),
                  at("recipe.yml", "requirements.check[0]", 14)),
              requirement_declaration(
                  requirement_scope::run(),
                  requirement_subject(package_reference("libfoo")),
                  at("recipe.yml", "requirements.run[0]", 16)),
          },
          {},
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("x86_64")}),
          at("recipe.yml", "$", 1)),
      profiles());
}

inline std::vector<pkgbuild::materialized_source> materials(
    const pkgsource::source_snapshot& source)
{
  std::vector<pkgbuild::materialized_source> result;
  for (const auto& input : source.recipe().sources())
    result.push_back(pkgbuild::materialized_source::verify(
        input, pkgbuild::sha256_digest(input.content_digest().hex())));
  return result;
}

inline pkgbuild::materialized_package_input package_input(
    pkgbuild::input_scope scope, const char* name, char seed)
{
  const std::string hex(64, seed);
  return pkgbuild::materialized_package_input(
      pkgbuild::resolved_package_input::make(
          scope, pkgsource::package_reference(name),
          pkgsource::package_release(pkgsource::package_reference(name),
                                     "1.0", 1),
          pkgsource::source_snapshot_identity::from_sha256(hex),
          pkgbuild::build_result_identity::from_sha256(hex),
          pkgbuild::artifact_identity::from_sha256(hex)),
      pkgbuild::input_tree_identity::from_sha256(hex));
}

inline std::vector<pkgbuild::materialized_package_input> inputs()
{
  return {
      package_input(pkgbuild::input_scope::build, "binutils", 'c'),
      package_input(pkgbuild::input_scope::build, "gcc", 'd'),
      package_input(pkgbuild::input_scope::check, "pkgcheck", 'e'),
  };
}

inline pkgbuild::build_policy policy()
{
  return pkgbuild::build_policy::make(
      pkgbuild::environment_policy::hermetic(4, 0022, 1700000000));
}

inline pkgbuild::build_request request()
{
  auto snapshot = source();
  return pkgbuild::build_request::seal(
      snapshot, materials(snapshot), inputs(),
      pkgsource::architecture_reference("x86_64"),
      pkgsource::architecture_reference("x86_64"), policy());
}

inline pkgbuild::payload_manifest payload()
{
  using namespace pkgbuild;
  return payload_manifest::seal({
      payload_entry::directory(payload_path::parse("usr/bin"), 0755, 0, 0,
                               payload_time{1700000000, 0}),
      payload_entry::regular(payload_path::parse("usr/bin/example"), 0755,
                             0, 0, 3, payload_time{1700000000, 1},
                             sha256_digest(std::string(64, 'f'))),
      payload_entry::hardlink(payload_path::parse("usr/bin/example-link"),
                              0755, 0, 0,
                              payload_time{1700000000, 1},
                              payload_path::parse("usr/bin/example")),
      payload_entry::symlink(payload_path::parse("usr/bin/example-symlink"),
                             0777, 0, 0,
                             payload_time{1700000000, 0}, "example"),
  });
}

inline pkgbuild::sealed_artifact artifact(char seed = '1')
{
  return pkgbuild::sealed_artifact::make(
      pkgbuild::artifact_encoding::package_tar_v1,
      pkgbuild::artifact_compression::zstd, 4096,
      pkgbuild::sha256_digest(std::string(64, seed)));
}

} // namespace fixture
