// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/libpkgbuild.h>

#include <cassert>
#include <functional>

namespace {
template<typename Function>
void expect(pkgbuild::error_code code, Function&& function)
{
  try { function(); assert(false); }
  catch (const pkgbuild::error& value) { assert(value.code() == code); }
}
}

int main()
{
  const pkgsource::source_input declaration = pkgsource::source_input::remote(
      "https://example.invalid/source", "source.tar",
      pkgsource::digest(pkgsource::digest_algorithm::sha256,
                        std::string(64, 'a')));
  const auto material = pkgbuild::materialized_source::verify(
      declaration, pkgbuild::sha256_digest(std::string(64, 'a')));
  assert(material.identity().hex().size() == 64);
  expect(pkgbuild::error_code::invalid_model, [&] {
    (void)pkgbuild::materialized_source::verify(
        declaration, pkgbuild::sha256_digest(std::string(64, 'b')));
  });
  assert(pkgbuild::payload_path::parse("./usr//bin/").string() == "usr/bin");
  expect(pkgbuild::error_code::invalid_model, [] {
    (void)pkgbuild::payload_path::parse("../etc/passwd");
  });
  const auto environment = pkgbuild::environment_policy::hermetic(4);
  assert(environment.network() == pkgbuild::network_policy::denied);
}
