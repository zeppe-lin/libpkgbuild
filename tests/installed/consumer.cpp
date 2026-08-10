// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/libpkgbuild.h>
#include <libpkgcatalog/libpkgcatalog.h>
#include <libpkgresolve/libpkgresolve.h>
#include <libpkgstate/libpkgstate.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

pkgsource::declaration_provenance at(const char* document,
                                     const char* path,
                                     std::uint32_t line)
{
  return pkgsource::declaration_provenance(document, path, line, 3);
}

pkgsource::profile_catalog profiles()
{
  using namespace pkgsource;
  return profile_catalog::seal({
      profile_declaration(
          profile_reference("@toolchain"), at("profiles.yml", "toolchain", 1),
          {profile_member_declaration(
               requirement_subject(package_reference("binutils")),
               at("profiles.yml", "toolchain[0]", 2)),
           profile_member_declaration(
               requirement_subject(package_reference("gcc")),
               at("profiles.yml", "toolchain[1]", 3))}),
  });
}

std::vector<pkgsource::architecture_reference> architectures(
    std::vector<std::string> names)
{
  std::vector<pkgsource::architecture_reference> result;
  for (auto& name : names)
    result.emplace_back(std::move(name));
  return result;
}

pkgsource::source_snapshot source(
    const pkgsource::profile_catalog& catalog,
    std::string name,
    std::vector<pkgsource::requirement_declaration> requirements = {},
    bool with_sources = false)
{
  using namespace pkgsource;
  std::vector<source_input> sources;
  if (with_sources) {
    sources.push_back(source_input::remote(
        "https://example.invalid/example.tar.xz", "example.tar.xz",
        digest(digest_algorithm::sha256, std::string(64, 'a'))));
  }
  return seal_source(
      source_origin(name + "/recipe.yml"),
      recipe_declaration(
          package_release(package_reference(name), "1.0", 1),
          package_metadata(name, std::nullopt, std::nullopt, {"MIT"}),
          std::move(sources),
          program(program_language::posix_shell, "build\n"),
          std::move(requirements), {},
          architecture_requirements(architectures({"x86_64"}),
                                    architectures({"x86_64"})),
          at("recipe.yml", "$", 1),
          program(program_language::posix_shell, "check\n")),
      catalog);
}

pkgstate::sha256_digest_bytes bytes(std::uint8_t seed)
{
  pkgstate::sha256_digest_bytes result{};
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = static_cast<std::uint8_t>(seed + i);
  return result;
}

template<typename Identity>
Identity state_identity(std::uint8_t seed)
{
  return Identity::from_sha256(bytes(seed));
}

pkgstate::snapshot empty_state()
{
  return pkgstate::snapshot::make(pkgstate::state_target_binding::make(
      state_identity<pkgstate::managed_target_identity>(1),
      state_identity<pkgstate::state_store_identity>(2),
      state_identity<pkgstate::root_view_identity>(3),
      state_identity<pkgstate::state_backend_identity>(4),
      state_identity<pkgstate::publication_domain_identity>(5)));
}

pkgcatalog::catalog_snapshot catalog(
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
  collections.emplace_back(0, pkgcatalog::seal_collection(std::move(declaration)));
  return pkgcatalog::catalog_snapshot::seal(
      std::move(profile_catalog), std::move(collections));
}

pkgresolve::resolution_result resolution()
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
  };
  std::vector<source_snapshot> sources;
  sources.push_back(source(profile_catalog, "example", requirements, true));
  for (const char* name : {"binutils", "gcc", "pkgcheck"})
    sources.push_back(source(profile_catalog, name));
  std::vector<pkgresolve::resolution_goal> goals;
  goals.emplace_back(requirement_scope::build(),
                     requirement_subject(package_reference("example")),
                     "installed-build");
  goals.emplace_back(requirement_scope::check(),
                     requirement_subject(package_reference("example")),
                     "installed-check");
  auto request = pkgresolve::resolution_request::seal(
      catalog(profile_catalog, std::move(sources)), empty_state(),
      pkgresolve::architecture_context(architecture_reference("x86_64"),
                                       architecture_reference("x86_64")),
      std::move(goals), pkgresolve::resolution_policy());
  return pkgresolve::resolve(std::move(request));
}

const pkgresolve::selected_package& subject(
    const pkgresolve::resolution_result& value)
{
  for (const auto& selection : value.selections()) {
    if (selection.environment() == pkgresolve::resolution_environment::target &&
        selection.package().name() == "example")
      return selection;
  }
  throw std::runtime_error("installed consumer lacks example subject");
}

} // namespace

int main()
{
  auto resolved = resolution();
  auto policy = pkgbuild::build_policy::make(
      pkgbuild::environment_policy::hermetic(2, 0022, 1700000000));
  auto request = pkgbuild::build_request::seal(
      resolved, subject(resolved).identity(), std::move(policy));
  auto payload = pkgbuild::payload_manifest::seal({
      pkgbuild::payload_entry::directory(
          pkgbuild::payload_path::parse("usr/bin"), 0755, 0, 0,
          pkgbuild::payload_time{1700000000, 0}),
      pkgbuild::payload_entry::regular(
          pkgbuild::payload_path::parse("usr/bin/example"), 0755, 0, 0, 3,
          pkgbuild::payload_time{1700000000, 0},
          pkgbuild::sha256_digest(std::string(64, 'f'))),
  });
  auto artifact = pkgbuild::sealed_artifact::make(
      pkgbuild::artifact_encoding::package_tar,
      pkgbuild::artifact_compression::zstd, 4096,
      pkgbuild::sha256_digest(std::string(64, '1')));
  auto result = pkgbuild::build_result::succeeded(
      request, std::move(payload), std::move(artifact),
      pkgbuild::execution_evidence_identity::from_sha256(std::string(64, '2')));
  if (result.outcome() != pkgbuild::build_outcome::succeeded ||
      result.request().identity() != request.identity())
    return 1;
  try {
    (void)pkgbuild::sealed_artifact::make(
        static_cast<pkgbuild::artifact_encoding>(255),
        pkgbuild::artifact_compression::zstd, 1,
        pkgbuild::sha256_digest(std::string(64, '3')));
  } catch (const pkgbuild::error& e) {
    return e.code() == pkgbuild::error_code::invalid_model ? 0 : 2;
  }
  return 3;
}
