// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.h"

#include <cassert>
#include <functional>

namespace {

template<typename Function>
void expect(pkgbuild::error_code code, Function&& function)
{
  try {
    function();
    assert(false);
  } catch (const pkgbuild::error& value) {
    assert(value.code() == code);
  }
}

void test_source_and_input_authority()
{
  auto snapshot = fixture::source();
  auto materials = fixture::materials(snapshot);
  assert(materials.size() == 2);
  assert(materials[0].identity().hex().size() == 64);

  expect(pkgbuild::error_code::invalid_model, [&] {
    (void)pkgbuild::materialized_source::verify(
        snapshot.recipe().sources()[0],
        pkgbuild::sha256_digest(std::string(64, '0')));
  });
  expect(pkgbuild::error_code::invalid_model, [] {
    (void)pkgbuild::resolved_package_input::make(
        pkgbuild::input_scope::build,
        pkgsource::package_reference("gcc"),
        pkgsource::package_release(pkgsource::package_reference("clang"),
                                   "1", 1),
        pkgsource::source_snapshot_identity::from_sha256(std::string(64, '1')),
        pkgbuild::build_result_identity::from_sha256(std::string(64, '2')),
        pkgbuild::artifact_identity::from_sha256(std::string(64, '3')));
  });
}

void test_environment_policy()
{
  const auto environment =
      pkgbuild::environment_policy::hermetic(8, 0027, 1700000000);
  assert(environment.locale() == pkgbuild::locale_policy::c_utf8);
  assert(environment.timezone() == pkgbuild::timezone_policy::utc);
  assert(environment.network() == pkgbuild::network_policy::denied);
  assert(environment.home() == pkgbuild::home_policy::isolated);
  assert(environment.parallelism() == 8);
  expect(pkgbuild::error_code::invalid_model, [] {
    (void)pkgbuild::environment_policy::hermetic(0);
  });
  expect(pkgbuild::error_code::invalid_model, [] {
    (void)pkgbuild::environment_policy::hermetic(1, 01000);
  });
}

void test_payload_model()
{
  assert(pkgbuild::payload_path::parse("./usr//bin/").string() == "usr/bin");
  expect(pkgbuild::error_code::invalid_model, [] {
    (void)pkgbuild::payload_path::parse("../etc/passwd");
  });
  expect(pkgbuild::error_code::invalid_model, [] {
    auto entry = pkgbuild::payload_entry::regular(
        pkgbuild::payload_path::parse("a"), 0644, 0, 0, 1,
        pkgbuild::payload_time{},
        pkgbuild::sha256_digest(std::string(64, 'a')));
    (void)pkgbuild::payload_manifest::seal({entry, entry});
  });
  expect(pkgbuild::error_code::invalid_model, [] {
    (void)pkgbuild::payload_manifest::seal({
        pkgbuild::payload_entry::hardlink(
            pkgbuild::payload_path::parse("a"), 0644, 0, 0,
            pkgbuild::payload_time{}, pkgbuild::payload_path::parse("missing")),
    });
  });
  const auto payload = fixture::payload();
  assert(payload.entries().size() == 4);
  assert(payload.identity().hex().size() == 64);
}

void test_artifact_authority()
{
  const auto artifact = fixture::artifact();
  assert(artifact.byte_count() == 4096);
  assert(artifact.complete_digest().hex() == std::string(64, '1'));
  assert(artifact.identity().hex() != artifact.complete_digest().hex());
}

} // namespace

int main()
{
  test_source_and_input_authority();
  test_environment_policy();
  test_payload_model();
  test_artifact_authority();
}
