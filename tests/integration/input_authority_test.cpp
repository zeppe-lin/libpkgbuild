// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../fixtures/build.h"
#include "../support/test.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

const pkgresolve::selected_package& require_selection(
    const pkgresolve::resolution_result& resolution,
    std::string_view package,
    pkgresolve::resolution_environment environment)
{
  const pkgresolve::selected_package* found = nullptr;
  for (const auto& selection : resolution.selections()) {
    if (selection.package().name() != package ||
        selection.environment() != environment)
      continue;
    TEST_CHECK(found == nullptr);
    found = &selection;
  }
  TEST_CHECK(found != nullptr);
  return *found;
}

void require_invalid_message(std::string_view expected,
                             const pkgresolve::resolution_result& resolution,
                             const pkgresolve::package_selection_identity& subject)
{
  bool caught = false;
  try {
    (void)pkgbuild::build_input_set::admit(resolution, subject);
  } catch (const pkgbuild::error& value) {
    caught = true;
    TEST_CHECK(value.code() == pkgbuild::error_code::invalid_request);
    TEST_CHECK(std::string_view(value.what()) == expected);
  }
  TEST_CHECK(caught);
}

pkgresolve::resolution_result without_direct_edge(
    const pkgresolve::resolution_result& value,
    const pkgresolve::package_selection_identity& issuer,
    std::string_view package,
    pkgsource::requirement_scope_kind scope)
{
  auto edges = value.edges();
  const auto found = std::find_if(edges.begin(), edges.end(), [&](const auto& edge) {
    if (edge.issuer() != issuer ||
        edge.environment() != pkgresolve::resolution_environment::build ||
        edge.scope().kind() != scope)
      return false;
    return require_selection(value, package,
                             pkgresolve::resolution_environment::build).identity()
        == edge.required();
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

pkgresolve::resolution_result transitive_check_resolution()
{
  using namespace pkgsource;
  auto profile_catalog = fixture::profiles();
  auto root = fixture::source(
      profile_catalog, "root",
      {requirement_declaration(
           requirement_scope::build(),
           requirement_subject(package_reference("checked-dependency")),
           fixture::at("recipe.yml", "requirements.build[0]", 10)),
       requirement_declaration(
           requirement_scope::build(),
           requirement_subject(package_reference("check-helper")),
           fixture::at("recipe.yml", "requirements.build[1]", 11))});
  auto checked_dependency = fixture::source(
      profile_catalog, "checked-dependency",
      {requirement_declaration(
           requirement_scope::build(),
           requirement_subject(package_reference("build-helper")),
           fixture::at("recipe.yml", "requirements.build[0]", 10)),
       requirement_declaration(
           requirement_scope::check(),
           requirement_subject(package_reference("check-helper")),
           fixture::at("recipe.yml", "requirements.check[0]", 12))});
  auto build_helper = fixture::source(profile_catalog, "build-helper");
  auto check_helper = fixture::source(profile_catalog, "check-helper");

  auto request = pkgresolve::resolution_request::seal(
      fixture::catalog(
          profile_catalog,
          {root, checked_dependency, build_helper, check_helper}),
      fixture::empty_state(),
      pkgresolve::architecture_context(
          architecture_reference("x86_64"), architecture_reference("x86_64")),
      {pkgresolve::resolution_goal(
           requirement_scope::build(),
           requirement_subject(package_reference("root")), "root-build"),
       pkgresolve::resolution_goal(
           requirement_scope::check(),
           requirement_subject(package_reference("root")), "root-check")},
      pkgresolve::resolution_policy());
  return pkgresolve::resolve(std::move(request));
}

pkgresolve::resolution_result with_fabricated_dormant_check_edge(
    const pkgresolve::resolution_result& value)
{
  auto edges = value.edges();
  const auto& issuer = require_selection(
      value, "checked-dependency", pkgresolve::resolution_environment::build);
  const auto& required = require_selection(
      value, "check-helper", pkgresolve::resolution_environment::build);
  TEST_CHECK(issuer.candidate() != nullptr);
  const auto checks = issuer.candidate()->source().recipe().check_requirements();
  TEST_CHECK(checks.size() == 1);
  edges.emplace_back(
      issuer.identity(), required.identity(), checks.front().scope(),
      pkgresolve::resolution_environment::build,
      pkgresolve::requirement_witness::catalog(
          issuer.candidate()->source().identity(), checks.front().origins()),
      pkgresolve::requirement_edge_identity::from_sha256(std::string(64, '9')));
  return pkgresolve::resolution_result(
      value.request(), value.selections(), std::move(edges), value.goals(),
      value.reasons(), pkgresolve::resolution_result_identity::from_sha256(
                           std::string(64, '8')));
}

pkgresolve::resolution_result profile_check_resolution()
{
  using namespace pkgsource;
  auto profiles = profile_catalog::seal({profile_declaration(
      profile_reference("@checked"), fixture::at("profiles.yml", "checked", 1),
      {profile_member_declaration(
          requirement_subject(package_reference("checked-dependency")),
          fixture::at("profiles.yml", "checked[0]", 2))})});
  auto checked_dependency = fixture::source(
      profiles, "checked-dependency",
      {requirement_declaration(
           requirement_scope::build(),
           requirement_subject(package_reference("build-helper")),
           fixture::at("recipe.yml", "requirements.build[0]", 10)),
       requirement_declaration(
           requirement_scope::check(),
           requirement_subject(package_reference("check-helper")),
           fixture::at("recipe.yml", "requirements.check[0]", 12))});
  auto build_helper = fixture::source(profiles, "build-helper");
  auto check_helper = fixture::source(profiles, "check-helper");

  auto request = pkgresolve::resolution_request::seal(
      fixture::catalog(
          profiles, {checked_dependency, build_helper, check_helper}),
      fixture::empty_state(),
      pkgresolve::architecture_context(
          architecture_reference("x86_64"), architecture_reference("x86_64")),
      {pkgresolve::resolution_goal(
          requirement_scope::check(),
          requirement_subject(profile_reference("@checked")), "profile-check")},
      pkgresolve::resolution_policy());
  return pkgresolve::resolve(std::move(request));
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

  // A transitive package is constructed, but it is not itself a member of any
  // admitted CHECK goal. Its declared CHECK dependency therefore remains
  // dormant while its BUILD dependency is exact active authority.
  const auto transitive = transitive_check_resolution();
  const auto& transitive_subject = require_selection(
      transitive, "checked-dependency",
      pkgresolve::resolution_environment::build);
  TEST_CHECK(transitive.edges_for_scope(
                 pkgsource::requirement_scope::check()).empty());
  const auto transitive_inputs = pkgbuild::build_input_set::admit(
      transitive, transitive_subject.identity());
  TEST_CHECK(transitive_inputs.inputs().size() == 1);
  TEST_CHECK(transitive_inputs.for_scope(pkgbuild::input_scope::build).size() == 1);
  TEST_CHECK(transitive_inputs.for_scope(pkgbuild::input_scope::check).empty());
  TEST_CHECK(transitive_inputs.inputs().front().package().name() ==
             "build-helper");

  // Resolver authority cannot manufacture a dormant CHECK edge either. If an
  // edge exists for a scope that this exact subject did not activate, admission
  // must reject it as unconsumed direct input authority.
  const auto fabricated = with_fabricated_dormant_check_edge(transitive);
  const auto& fabricated_subject = require_selection(
      fabricated, "checked-dependency",
      pkgresolve::resolution_environment::build);
  require_invalid_message(
      "resolution contains an undeclared direct build input",
      fabricated, fabricated_subject.identity());

  // Profile goals activate CHECK by exact resolved membership just as direct
  // package goals do; profile syntax is not a second authority model.
  const auto profile = profile_check_resolution();
  const auto& profile_subject = require_selection(
      profile, "checked-dependency",
      pkgresolve::resolution_environment::target);
  TEST_CHECK(profile.goals().size() == 1);
  TEST_CHECK(profile.goals().front().goal().scope().kind() ==
             pkgsource::requirement_scope_kind::check);
  TEST_CHECK(profile.goals().front().members().size() == 1);
  TEST_CHECK(profile.goals().front().members().front().selection() ==
             profile_subject.identity());
  const auto profile_inputs = pkgbuild::build_input_set::admit(
      profile, profile_subject.identity());
  TEST_CHECK(profile_inputs.for_scope(pkgbuild::input_scope::build).size() == 1);
  TEST_CHECK(profile_inputs.for_scope(pkgbuild::input_scope::check).size() == 1);
  TEST_CHECK(profile_inputs.for_scope(pkgbuild::input_scope::check)
                 .front().package().name() == "check-helper");

  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::build_input_set::admit(
          resolved, pkgresolve::package_selection_identity::from_sha256(
                        std::string(64, '0'))));

  // Missing active BUILD authority fails closed by its exact edge.
  const auto missing_build = without_direct_edge(
      resolved, subject.identity(), "binutils",
      pkgsource::requirement_scope_kind::build);
  require_invalid_message(
      "build requirement lacks exact resolver authority",
      missing_build, fixture::subject(missing_build).identity());

  // Missing active CHECK authority is independently named and rejected. This
  // prevents the dormant-CHECK rule from degenerating into "CHECK is optional".
  const auto missing_check = without_direct_edge(
      resolved, subject.identity(), "pkgcheck",
      pkgsource::requirement_scope_kind::check);
  require_invalid_message(
      "build requirement lacks exact resolver authority",
      missing_check, fixture::subject(missing_check).identity());

  const auto duplicate = with_duplicate_direct_edge(resolved);
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::build_input_set::admit(
          duplicate, fixture::subject(duplicate).identity()));
}
