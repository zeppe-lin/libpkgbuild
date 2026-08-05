// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <libpkgbuild/libpkgbuild.h>
#include <libpkgcatalog/libpkgcatalog.h>
#include <libpkgresolve/libpkgresolve.h>
#include <libpkgstate/libpkgstate.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fixture {

inline pkgsource::declaration_provenance at(
    const char* document, const char* path, std::uint32_t line)
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

inline pkgsource::source_snapshot source(
    const pkgsource::profile_catalog& catalog,
    std::string name,
    std::vector<pkgsource::requirement_declaration> requirements = {},
    bool payload = false)
{
  using namespace pkgsource;
  std::vector<source_input> sources;
  if (payload) {
    sources.push_back(source_input::remote(
        "https://example.invalid/example.tar.xz", "example.tar.xz",
        digest(digest_algorithm::sha256, std::string(64, 'a'))));
    sources.push_back(source_input::local(
        "files/example.conf", "example.conf",
        digest(digest_algorithm::sha256, std::string(64, 'b'))));
  }
  return seal_source(
      source_origin(name + "/recipe.yml"),
      recipe_declaration(
          package_release(package_reference(name), "1.2.3", 1),
          package_metadata(name, std::nullopt, std::nullopt, {"MIT"}),
          std::move(sources),
          program(program_language::posix_shell,
                  "meson setup build\nmeson compile -C build\n"),
          std::move(requirements), {},
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("x86_64")}),
          at("recipe.yml", "$", 1),
          program(program_language::posix_shell,
                  "meson test -C build\n")),
      catalog);
}

inline pkgstate::sha256_digest_bytes state_bytes(std::uint8_t seed)
{
  pkgstate::sha256_digest_bytes result{};
  for (std::size_t index = 0; index < result.size(); ++index)
    result[index] = static_cast<std::uint8_t>(seed + index);
  return result;
}

template<typename Identity>
Identity state_identity(std::uint8_t seed)
{
  return Identity::from_sha256(state_bytes(seed));
}

inline pkgstate::snapshot empty_state()
{
  return pkgstate::snapshot::make(pkgstate::state_target_binding::make(
      state_identity<pkgstate::managed_target_identity>(1),
      state_identity<pkgstate::state_store_identity>(2),
      state_identity<pkgstate::root_view_identity>(3),
      state_identity<pkgstate::state_backend_identity>(4),
      state_identity<pkgstate::publication_domain_identity>(5)));
}

inline pkgresolve::resolution_result resolution()
{
  using namespace pkgsource;
  auto profile_catalog = profiles();
  std::vector<requirement_declaration> requirements{
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
  };
  std::vector<source_snapshot> sources;
  sources.push_back(source(profile_catalog, "example", requirements, true));
  for (const char* name : {"binutils", "gcc", "pkgcheck", "libfoo"})
    sources.push_back(source(profile_catalog, name));

  pkgcatalog::collection_declaration declaration(
      pkgcatalog::collection_reference("core"),
      pkgcatalog::collection_provenance(
          "/collections/core", std::nullopt,
          at("catalog.yml", "collections[0]", 1)),
      std::move(sources));
  std::vector<pkgcatalog::catalog_collection> collections;
  collections.emplace_back(
      0, pkgcatalog::seal_collection(std::move(declaration)));
  auto catalog = pkgcatalog::catalog_snapshot::seal(
      profile_catalog, std::move(collections));

  std::vector<pkgresolve::resolution_goal> goals;
  goals.emplace_back(
      requirement_scope::build(),
      requirement_subject(package_reference("example")), "test-build");
  goals.emplace_back(
      requirement_scope::check(),
      requirement_subject(package_reference("example")), "test-check");
  auto request = pkgresolve::resolution_request::seal(
      std::move(catalog), empty_state(),
      pkgresolve::architecture_context(
          architecture_reference("x86_64"),
          architecture_reference("x86_64")),
      std::move(goals), pkgresolve::resolution_policy());
  return pkgresolve::resolve(std::move(request));
}

inline const pkgresolve::selected_package& subject(
    const pkgresolve::resolution_result& resolution)
{
  for (const auto& selection : resolution.selections())
    if (selection.environment() == pkgresolve::resolution_environment::target &&
        selection.package().name() == "example")
      return selection;
  throw std::runtime_error("fixture resolution lacks example subject");
}

inline pkgbuild::build_policy policy()
{
  return pkgbuild::build_policy::make(
      pkgbuild::environment_policy::hermetic(4, 0022, 1700000000));
}

inline pkgbuild::build_request request()
{
  auto resolved = resolution();
  return pkgbuild::build_request::seal(
      resolved, subject(resolved).identity(), policy());
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
                              0755, 0, 0, payload_time{1700000000, 1},
                              payload_path::parse("usr/bin/example")),
      payload_entry::symlink(payload_path::parse("usr/bin/example-symlink"),
                             0777, 0, 0, payload_time{1700000000, 0},
                             "example"),
  });
}

inline pkgbuild::sealed_artifact artifact(char seed = '1')
{
  return pkgbuild::sealed_artifact::make(
      pkgbuild::artifact_encoding::package_tar,
      pkgbuild::artifact_compression::zstd, 4096,
      pkgbuild::sha256_digest(std::string(64, seed)));
}

} // namespace fixture
