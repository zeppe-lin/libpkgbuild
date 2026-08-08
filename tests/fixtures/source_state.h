// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

inline std::vector<pkgsource::architecture_reference> architectures(
    std::vector<std::string> names)
{
  std::vector<pkgsource::architecture_reference> result;
  result.reserve(names.size());
  for (auto& name : names)
    result.emplace_back(std::move(name));
  return result;
}

inline pkgsource::source_snapshot source(
    const pkgsource::profile_catalog& catalog,
    std::string name,
    std::vector<pkgsource::requirement_declaration> requirements = {},
    bool with_sources = false,
    std::vector<std::string> build_architectures = {"x86_64"},
    std::vector<std::string> target_architectures = {"x86_64"},
    std::string build_program = "meson setup build\nmeson compile -C build\n")
{
  using namespace pkgsource;
  std::vector<source_input> sources;
  if (with_sources) {
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
          program(program_language::posix_shell, std::move(build_program)),
          std::move(requirements), {},
          architecture_requirements(
              architectures(std::move(build_architectures)),
              architectures(std::move(target_architectures))),
          at("recipe.yml", "$", 1),
          program(program_language::posix_shell, "meson test -C build\n")),
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

inline pkgcatalog::catalog_snapshot catalog(
    pkgsource::profile_catalog profile_catalog,
    std::vector<pkgsource::source_snapshot> sources)
{
  pkgcatalog::collection_declaration declaration(
      pkgcatalog::collection_reference("core"),
      pkgcatalog::collection_provenance(
          "/collections/core", std::nullopt,
          at("catalog.yml", "collections[0]", 1)),
      std::move(sources));
  std::vector<pkgcatalog::catalog_collection> collections;
  collections.emplace_back(
      0, pkgcatalog::seal_collection(std::move(declaration)));
  return pkgcatalog::catalog_snapshot::seal(
      std::move(profile_catalog), std::move(collections));
}

} // namespace fixture
