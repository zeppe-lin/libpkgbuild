// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../fixtures/build.h"
#include "../support/test.h"

#include <string>
#include <vector>

namespace {

pkgresolve::resolution_result with_contradictory_subject_source(
    const pkgresolve::resolution_result& value)
{
  auto selections = value.selections();
  const auto identity = fixture::subject(value).identity();
  for (auto& selection : selections) {
    if (selection.identity() != identity)
      continue;
    TEST_CHECK(selection.candidate() != nullptr);
    selection = pkgresolve::selected_package(
        selection.environment(), selection.architectures(),
        pkgresolve::selection_authority(*selection.candidate()),
        selection.release(),
        pkgsource::source_snapshot_identity::from_sha256(std::string(64, '1')),
        selection.identity());
  }
  return pkgresolve::resolution_result(
      value.request(), std::move(selections), value.edges(), value.goals(),
      value.reasons(), pkgresolve::resolution_result_identity::from_sha256(
                           std::string(64, '2')));
}

} // namespace

int main()
{
  auto resolved = fixture::resolution();
  const auto& selected = fixture::subject(resolved);
  const auto request = pkgbuild::build_request::seal(
      resolved, selected.identity(), fixture::policy());

  TEST_CHECK(request.subject().identity() == selected.identity());
  TEST_CHECK(request.source().identity() == selected.source_snapshot());
  TEST_CHECK(request.release().identity() == selected.release().identity());
  TEST_CHECK(request.selected_profiles().size() == 1);
  TEST_CHECK(request.selected_profiles().front().profile().name() == "@toolchain");
  TEST_CHECK(request.build_program().material().find("meson compile") !=
             std::string::npos);
  TEST_CHECK(request.policy().environment().network() ==
             pkgbuild::network_policy::denied);
  TEST_CHECK(sizeof(pkgbuild::build_request) == sizeof(void*) * 2U);

  auto contradictory = with_contradictory_subject_source(resolved);
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::build_request::seal(
          contradictory, fixture::subject(contradictory).identity(),
          fixture::policy()));
}
