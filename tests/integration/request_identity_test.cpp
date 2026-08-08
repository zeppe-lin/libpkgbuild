// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../fixtures/build.h"
#include "../support/test.h"

#include <string>

int main()
{
  auto first_resolution = fixture::resolution();
  const auto first = pkgbuild::build_request::seal(
      first_resolution, fixture::subject(first_resolution).identity(),
      fixture::policy());
  const auto repeated = pkgbuild::build_request::seal(
      first_resolution, fixture::subject(first_resolution).identity(),
      fixture::policy());
  const auto changed_policy = pkgbuild::build_request::seal(
      first_resolution, fixture::subject(first_resolution).identity(),
      fixture::policy(5));

  auto rebound_resolution = pkgresolve::resolution_result(
      first_resolution.request(), first_resolution.selections(),
      first_resolution.edges(), first_resolution.goals(),
      first_resolution.reasons(),
      pkgresolve::resolution_result_identity::from_sha256(std::string(64, 'c')));
  const auto changed_authority = pkgbuild::build_request::seal(
      rebound_resolution, fixture::subject(rebound_resolution).identity(),
      fixture::policy());

  TEST_CHECK(first.identity() == repeated.identity());
  TEST_CHECK(first.identity() != changed_policy.identity());
  TEST_CHECK(first.inputs().resolution() != changed_authority.inputs().resolution());
  TEST_CHECK(first.identity() != changed_authority.identity());
}
