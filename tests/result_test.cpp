// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.h"

#include <cassert>

int main()
{
  const auto execution = pkgbuild::execution_evidence_identity::from_sha256(
      std::string(64, '8'));
  const auto success = pkgbuild::build_result::succeeded(
      fixture::request(), fixture::payload(), fixture::artifact(), execution);
  assert(success.outcome() == pkgbuild::build_outcome::succeeded);
  assert(success.payload().has_value());
  assert(success.artifact().has_value());
  assert(success.artifact_binding().has_value());
  assert(!success.failure_evidence().has_value());

  const auto changed = pkgbuild::build_result::succeeded(
      fixture::request(), fixture::payload(), fixture::artifact('2'), execution);
  assert(success.identity() != changed.identity());

  const auto failure = pkgbuild::build_result::failed(
      fixture::request(), execution,
      pkgbuild::failure_evidence_identity::from_sha256(std::string(64, '9')));
  assert(failure.outcome() == pkgbuild::build_outcome::failed);
  assert(!failure.payload().has_value());
  assert(!failure.artifact().has_value());
  assert(!failure.artifact_binding().has_value());
  assert(failure.failure_evidence().has_value());
  assert(success.identity() != failure.identity());
}
