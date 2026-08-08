// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../fixtures/build.h"
#include "../support/test.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace {

pkgresolve::resolution_result without_one_direct_edge(
    const pkgresolve::resolution_result& value)
{
  auto edges = value.edges();
  const auto subject = fixture::subject(value).identity();
  const auto found = std::find_if(edges.begin(), edges.end(), [&](const auto& edge) {
    return edge.issuer() == subject &&
           edge.environment() == pkgresolve::resolution_environment::build &&
           (edge.scope().kind() == pkgsource::requirement_scope_kind::build ||
            edge.scope().kind() == pkgsource::requirement_scope_kind::check);
  });
  TEST_CHECK(found != edges.end());
  edges.erase(found);
  return pkgresolve::resolution_result(
      value.request(), value.selections(), std::move(edges), value.goals(),
      value.reasons(), pkgresolve::resolution_result_identity::from_sha256(
                           std::string(64, 'd')));
}

pkgresolve::resolution_result with_duplicate_direct_edge(
    const pkgresolve::resolution_result& value)
{
  auto edges = value.edges();
  const auto subject = fixture::subject(value).identity();
  const auto found = std::find_if(edges.begin(), edges.end(), [&](const auto& edge) {
    return edge.issuer() == subject &&
           edge.environment() == pkgresolve::resolution_environment::build &&
           edge.scope().kind() == pkgsource::requirement_scope_kind::check;
  });
  TEST_CHECK(found != edges.end());
  edges.emplace_back(
      found->issuer(), found->required(), found->scope(), found->environment(),
      found->witness(), pkgresolve::requirement_edge_identity::from_sha256(
                            std::string(64, 'e')));
  return pkgresolve::resolution_result(
      value.request(), value.selections(), std::move(edges), value.goals(),
      value.reasons(), pkgresolve::resolution_result_identity::from_sha256(
                           std::string(64, 'f')));
}

} // namespace

int main()
{
  const auto resolved = fixture::resolution();
  const auto& subject = fixture::subject(resolved);
  const auto inputs = pkgbuild::build_input_set::admit(
      resolved, subject.identity());

  TEST_CHECK(inputs.subject().identity() == subject.identity());
  TEST_CHECK(inputs.resolution() == resolved.identity());
  TEST_CHECK(inputs.inputs().size() == 3);
  TEST_CHECK(inputs.for_scope(pkgbuild::input_scope::build).size() == 2);
  TEST_CHECK(inputs.for_scope(pkgbuild::input_scope::check).size() == 1);

  std::set<std::string> build_packages;
  std::set<std::string> check_packages;
  for (const auto& input : inputs.inputs()) {
    TEST_CHECK(input.requirement().issuer() == subject.identity());
    TEST_CHECK(input.requirement().required() == input.selection().identity());
    TEST_CHECK(input.requirement().environment() ==
               pkgresolve::resolution_environment::build);
    TEST_CHECK(input.requirement().witness().kind() ==
               pkgresolve::requirement_authority_kind::catalog_source);
    TEST_CHECK(input.requirement().witness().catalog_source() ==
               std::optional<pkgsource::source_snapshot_identity>(
                   subject.source_snapshot()));
    if (input.scope() == pkgbuild::input_scope::build)
      build_packages.insert(input.package().name());
    else
      check_packages.insert(input.package().name());
  }
  TEST_CHECK(build_packages == std::set<std::string>({"binutils", "gcc"}));
  TEST_CHECK(check_packages == std::set<std::string>({"pkgcheck"}));
  TEST_CHECK(build_packages.count("libfoo") == 0);
  TEST_CHECK(check_packages.count("libfoo") == 0);

  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::build_input_set::admit(
          resolved, pkgresolve::package_selection_identity::from_sha256(
                        std::string(64, '0'))));

  const auto missing = without_one_direct_edge(resolved);
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::build_input_set::admit(missing, fixture::subject(missing).identity()));

  const auto duplicate = with_duplicate_direct_edge(resolved);
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::build_input_set::admit(
          duplicate, fixture::subject(duplicate).identity()));
}
