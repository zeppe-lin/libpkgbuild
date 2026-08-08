// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../support/test.h"

#include <libpkgbuild/libpkgbuild.h>

#include <string>

template<typename Identity>
void check_identity(char seed)
{
  const std::string hex(64, seed);
  const auto value = Identity::from_sha256(hex);
  const auto same = Identity::from_sha256(hex);
  const char later_seed = seed == '9' ? 'a' : static_cast<char>(seed + 1);
  const auto later = Identity::from_sha256(std::string(64, later_seed));
  TEST_CHECK(value.hex() == hex);
  TEST_CHECK(value == same);
  TEST_CHECK(value != later);
  TEST_CHECK(value < later);
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_identity,
                       Identity::from_sha256("abcd"));
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_identity,
                       Identity::from_sha256(std::string(64, 'A')));
}

int main()
{
  check_identity<pkgbuild::build_input_identity>('1');
  check_identity<pkgbuild::build_input_set_identity>('2');
  check_identity<pkgbuild::environment_policy_identity>('3');
  check_identity<pkgbuild::build_policy_identity>('4');
  check_identity<pkgbuild::build_request_identity>('5');
  check_identity<pkgbuild::payload_manifest_identity>('6');
  check_identity<pkgbuild::artifact_identity>('7');
  check_identity<pkgbuild::artifact_binding_identity>('8');
  check_identity<pkgbuild::execution_evidence_identity>('9');
  check_identity<pkgbuild::failure_evidence_identity>('a');
  check_identity<pkgbuild::build_result_identity>('b');
}
