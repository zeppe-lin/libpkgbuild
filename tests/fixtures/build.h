// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "source_state.h"

#include <libpkgbuild/libpkgbuild.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fixture {

inline pkgresolve::resolution_result resolution(
    std::vector<std::string> subject_build_architectures = {"x86_64"},
    std::vector<std::string> subject_target_architectures = {"x86_64"},
    std::string selected_build = "x86_64",
    std::string selected_target = "x86_64",
    std::string goal_origin_suffix = {})
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
  sources.push_back(source(
      profile_catalog, "example", requirements, true,
      std::move(subject_build_architectures),
      std::move(subject_target_architectures)));
  for (const char* name : {"binutils", "gcc", "pkgcheck", "libfoo"})
    sources.push_back(source(profile_catalog, name, {}, false, {}, {}));

  std::vector<pkgresolve::resolution_goal> goals;
  goals.emplace_back(
      requirement_scope::build(),
      requirement_subject(package_reference("example")),
      "test-build" + goal_origin_suffix);
  goals.emplace_back(
      requirement_scope::check(),
      requirement_subject(package_reference("example")),
      "test-check" + goal_origin_suffix);

  auto request = pkgresolve::resolution_request::seal(
      catalog(profile_catalog, std::move(sources)), empty_state(),
      pkgresolve::architecture_context(
          architecture_reference(std::move(selected_build)),
          architecture_reference(std::move(selected_target))),
      std::move(goals), pkgresolve::resolution_policy());
  return pkgresolve::resolve(std::move(request));
}

inline const pkgresolve::selected_package& subject(
    const pkgresolve::resolution_result& resolution)
{
  for (const auto& selection : resolution.selections()) {
    if (selection.environment() == pkgresolve::resolution_environment::target &&
        selection.package().name() == "example")
      return selection;
  }
  throw std::runtime_error("fixture resolution lacks example subject");
}

inline pkgbuild::build_policy policy(
    std::uint32_t parallelism = 4,
    std::optional<std::int64_t> source_date_epoch = 1700000000)
{
  return pkgbuild::build_policy::make(
      pkgbuild::environment_policy::hermetic(
          parallelism, 0022, source_date_epoch));
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
