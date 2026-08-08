// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../fixtures/build.h"
#include "../support/test.h"

#include <string>

int main()
{
  const auto request = fixture::request();
  const auto execution = pkgbuild::execution_evidence_identity::from_sha256(
      std::string(64, '8'));
  const auto success = pkgbuild::build_result::succeeded(
      request, fixture::payload(), fixture::artifact(), execution);
  TEST_CHECK(success.outcome() == pkgbuild::build_outcome::succeeded);
  TEST_CHECK(success.request().identity() == request.identity());
  TEST_CHECK(success.execution_evidence() == execution);
  TEST_CHECK(success.payload().has_value());
  TEST_CHECK(success.artifact().has_value());
  TEST_CHECK(success.artifact_binding().has_value());
  TEST_CHECK(!success.failure_evidence().has_value());

  const auto changed_artifact = pkgbuild::build_result::succeeded(
      request, fixture::payload(), fixture::artifact('2'), execution);
  const auto changed_execution = pkgbuild::build_result::succeeded(
      request, fixture::payload(), fixture::artifact(),
      pkgbuild::execution_evidence_identity::from_sha256(std::string(64, '7')));
  TEST_CHECK(success.identity() != changed_artifact.identity());
  TEST_CHECK(success.artifact_binding() != changed_artifact.artifact_binding());
  TEST_CHECK(success.identity() != changed_execution.identity());
  TEST_CHECK(success.artifact_binding() == changed_execution.artifact_binding());

  const auto failure = pkgbuild::build_result::failed(
      request, execution,
      pkgbuild::failure_evidence_identity::from_sha256(std::string(64, '9')));
  const auto changed_failure = pkgbuild::build_result::failed(
      request, execution,
      pkgbuild::failure_evidence_identity::from_sha256(std::string(64, 'a')));
  TEST_CHECK(failure.outcome() == pkgbuild::build_outcome::failed);
  TEST_CHECK(!failure.payload().has_value());
  TEST_CHECK(!failure.artifact().has_value());
  TEST_CHECK(!failure.artifact_binding().has_value());
  TEST_CHECK(failure.failure_evidence().has_value());
  TEST_CHECK(success.identity() != failure.identity());
  TEST_CHECK(failure.identity() != changed_failure.identity());
}
