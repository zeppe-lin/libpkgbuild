// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/request.h>

#include <libpkgbuild/error.h>

#include "identity_support.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>

namespace pkgbuild {
namespace {

[[noreturn]] void invalid(std::string message)
{
  throw error(error_code::invalid_request, std::move(message));
}

const pkgresolve::selected_package& require_selection(
    const pkgresolve::resolution_result& resolution,
    const pkgresolve::package_selection_identity& identity)
{
  const pkgresolve::selected_package* result = nullptr;
  for (const auto& selection : resolution.selections()) {
    if (selection.identity() != identity)
      continue;
    if (result != nullptr)
      invalid("resolution duplicates a package selection identity");
    result = &selection;
  }
  if (result == nullptr)
    invalid("build subject or requirement names an absent package selection");
  return *result;
}

input_scope input_scope_from(const pkgsource::requirement_scope& scope)
{
  switch (scope.kind()) {
  case pkgsource::requirement_scope_kind::build:
    return input_scope::build;
  case pkgsource::requirement_scope_kind::check:
    return input_scope::check;
  default:
    invalid("build input uses a non-build requirement scope");
  }
}

build_input_identity input_id(
    input_scope scope,
    const pkgresolve::requirement_edge& edge,
    const pkgresolve::selected_package& selection)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-input/1");
  writer.text(to_string(scope));
  writer.text(edge.identity().hex());
  writer.text(selection.identity().hex());
  return build_input_identity::from_sha256(writer.finish());
}

build_input_set_identity input_set_id(
    const pkgresolve::resolution_result_identity& resolution,
    const pkgresolve::package_selection_identity& subject,
    const std::vector<build_input>& inputs)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-input-set/1");
  writer.text(resolution.hex());
  writer.text(subject.hex());
  writer.number(inputs.size());
  for (const auto& input : inputs)
    writer.text(input.identity().hex());
  return build_input_set_identity::from_sha256(writer.finish());
}

build_policy_identity policy_id(
    const environment_policy& environment,
    output_layout_kind output_layout)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-policy/1");
  writer.text(environment.identity().hex());
  writer.text(to_string(output_layout));
  return build_policy_identity::from_sha256(writer.finish());
}

build_request_identity request_id(
    const pkgsource::source_snapshot& source,
    const build_input_set& inputs,
    const architecture_binding& architectures,
    const build_policy& policy)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-request/1");
  writer.text(source.identity().hex());
  writer.text(inputs.identity().hex());
  writer.text(architectures.build().name());
  writer.text(architectures.target().name());
  writer.text(policy.identity().hex());
  return build_request_identity::from_sha256(writer.finish());
}

bool allows_architecture(
    const std::vector<pkgsource::architecture_reference>& declared,
    const pkgsource::architecture_reference& selected)
{
  return declared.empty() ||
      std::find(declared.begin(), declared.end(), selected) != declared.end();
}

std::vector<pkgsource::resolved_requirement> direct_requirements(
    const pkgsource::source_snapshot& source)
{
  auto result = source.recipe().build_requirements();
  auto checks = source.recipe().check_requirements();
  result.insert(result.end(), checks.begin(), checks.end());
  std::sort(result.begin(), result.end());
  return result;
}

} // namespace

struct build_input::impl final {
  impl(input_scope scope_value,
       pkgresolve::requirement_edge requirement_value,
       pkgresolve::selected_package selection_value,
       build_input_identity identity_value)
      : scope(scope_value), requirement(std::move(requirement_value)),
        selection(std::move(selection_value)), identity(std::move(identity_value))
  {
  }
  input_scope scope;
  pkgresolve::requirement_edge requirement;
  pkgresolve::selected_package selection;
  build_input_identity identity;
};

build_input::build_input(std::shared_ptr<const impl> value)
    : impl_(std::move(value))
{
}
build_input::build_input(const build_input&) noexcept = default;
build_input::build_input(build_input&&) noexcept = default;
build_input& build_input::operator=(const build_input&) noexcept = default;
build_input& build_input::operator=(build_input&&) noexcept = default;
build_input::~build_input() = default;
input_scope build_input::scope() const noexcept { return impl_->scope; }
const pkgresolve::requirement_edge& build_input::requirement() const noexcept
{ return impl_->requirement; }
const pkgresolve::selected_package& build_input::selection() const noexcept
{ return impl_->selection; }
const pkgsource::package_reference& build_input::package() const noexcept
{ return impl_->selection.package(); }
const build_input_identity& build_input::identity() const noexcept
{ return impl_->identity; }
bool operator==(const build_input& lhs, const build_input& rhs) noexcept
{ return lhs.identity() == rhs.identity(); }
bool operator!=(const build_input& lhs, const build_input& rhs) noexcept
{ return !(lhs == rhs); }
bool operator<(const build_input& lhs, const build_input& rhs) noexcept
{ return lhs.identity() < rhs.identity(); }

struct build_input_set::impl final {
  impl(pkgresolve::selected_package subject_value,
       pkgresolve::resolution_result_identity resolution_value,
       std::vector<build_input> inputs_value,
       build_input_set_identity identity_value)
      : subject(std::move(subject_value)), resolution(std::move(resolution_value)),
        inputs(std::move(inputs_value)), identity(std::move(identity_value))
  {
  }
  pkgresolve::selected_package subject;
  pkgresolve::resolution_result_identity resolution;
  std::vector<build_input> inputs;
  build_input_set_identity identity;
};

build_input_set::build_input_set(std::shared_ptr<const impl> value)
    : impl_(std::move(value))
{
}
build_input_set::build_input_set(const build_input_set&) noexcept = default;
build_input_set::build_input_set(build_input_set&&) noexcept = default;
build_input_set& build_input_set::operator=(const build_input_set&) noexcept = default;
build_input_set& build_input_set::operator=(build_input_set&&) noexcept = default;
build_input_set::~build_input_set() = default;

build_input_set build_input_set::admit(
    const pkgresolve::resolution_result& resolution,
    const pkgresolve::package_selection_identity& subject_identity)
{
  const auto& subject = require_selection(resolution, subject_identity);
  const auto* candidate = subject.candidate();
  if (candidate == nullptr)
    invalid("build subject must retain catalog source authority");
  if (candidate->source().identity() != subject.source_snapshot() ||
      candidate->release().identity() != subject.release().identity() ||
      candidate->package() != subject.package())
    invalid("build subject contradicts its catalog authority");
  const pkgresolve::architecture_context expected_architectures(
      resolution.request().architectures().build(),
      resolution.request().architectures().selected_target(
          subject.environment()));
  if (subject.architectures() != expected_architectures)
    invalid("build subject architecture context differs from resolution request");

  const auto requirements = direct_requirements(candidate->source());
  std::vector<build_input> inputs;
  std::set<pkgresolve::requirement_edge_identity> consumed;
  inputs.reserve(requirements.size());

  for (const auto& requirement : requirements) {
    const pkgresolve::requirement_edge* match = nullptr;
    const pkgresolve::selected_package* required = nullptr;
    for (const auto& edge : resolution.edges()) {
      if (edge.issuer() != subject_identity ||
          edge.environment() != pkgresolve::resolution_environment::build ||
          edge.scope() != requirement.scope())
        continue;
      const auto& selected = require_selection(resolution, edge.required());
      if (selected.package() != requirement.package())
        continue;
      if (match != nullptr)
        invalid("build requirement has ambiguous resolver authority");
      match = &edge;
      required = &selected;
    }
    if (match == nullptr || required == nullptr)
      invalid("build requirement lacks exact resolver authority");
    if (required->environment() != pkgresolve::resolution_environment::build)
      invalid("build requirement selected a non-build environment package");
    if (match->witness().kind() !=
            pkgresolve::requirement_authority_kind::catalog_source ||
        !match->witness().catalog_source() ||
        *match->witness().catalog_source() != candidate->source().identity() ||
        match->witness().catalog_origins() != requirement.origins())
      invalid("build requirement witness differs from source authority");
    if (!consumed.insert(match->identity()).second)
      invalid("resolver edge satisfies more than one build requirement");

    const auto scope = input_scope_from(requirement.scope());
    auto identity = input_id(scope, *match, *required);
    inputs.push_back(build_input(std::make_shared<build_input::impl>(
        scope, *match, *required, std::move(identity))));
  }

  for (const auto& edge : resolution.edges()) {
    if (edge.issuer() != subject_identity ||
        edge.environment() != pkgresolve::resolution_environment::build)
      continue;
    const auto kind = edge.scope().kind();
    if (kind != pkgsource::requirement_scope_kind::build &&
        kind != pkgsource::requirement_scope_kind::check)
      continue;
    if (consumed.find(edge.identity()) == consumed.end())
      invalid("resolution contains an undeclared direct build input");
  }

  std::sort(inputs.begin(), inputs.end());
  auto identity = input_set_id(resolution.identity(), subject_identity, inputs);
  return build_input_set(std::make_shared<impl>(
      subject, resolution.identity(), std::move(inputs), std::move(identity)));
}

const pkgresolve::selected_package& build_input_set::subject() const noexcept
{ return impl_->subject; }
const pkgresolve::resolution_result_identity&
build_input_set::resolution() const noexcept { return impl_->resolution; }
const std::vector<build_input>& build_input_set::inputs() const noexcept
{ return impl_->inputs; }
std::vector<build_input> build_input_set::for_scope(input_scope scope) const
{
  std::vector<build_input> result;
  for (const auto& input : impl_->inputs)
    if (input.scope() == scope)
      result.push_back(input);
  return result;
}
const build_input_set_identity& build_input_set::identity() const noexcept
{ return impl_->identity; }

struct architecture_binding::impl final {
  impl(pkgsource::architecture_reference build_value,
       pkgsource::architecture_reference target_value)
      : build(std::move(build_value)), target(std::move(target_value))
  {
  }
  pkgsource::architecture_reference build;
  pkgsource::architecture_reference target;
};

architecture_binding::architecture_binding(std::shared_ptr<const impl> value)
    : impl_(std::move(value))
{
}
architecture_binding::architecture_binding(const architecture_binding&) noexcept = default;
architecture_binding::architecture_binding(architecture_binding&&) noexcept = default;
architecture_binding& architecture_binding::operator=(const architecture_binding&) noexcept = default;
architecture_binding& architecture_binding::operator=(architecture_binding&&) noexcept = default;
architecture_binding::~architecture_binding() = default;
architecture_binding architecture_binding::select(
    const pkgsource::architecture_requirements& declared,
    pkgsource::architecture_reference build,
    pkgsource::architecture_reference target)
{
  if (!allows_architecture(declared.build(), build))
    invalid("selected build architecture is not declared by the source");
  if (!allows_architecture(declared.target(), target))
    invalid("selected target architecture is not declared by the source");
  return architecture_binding(std::make_shared<impl>(
      std::move(build), std::move(target)));
}
const pkgsource::architecture_reference& architecture_binding::build() const noexcept
{ return impl_->build; }
const pkgsource::architecture_reference& architecture_binding::target() const noexcept
{ return impl_->target; }

build_policy::build_policy(environment_policy environment,
                           output_layout_kind output_layout,
                           build_policy_identity identity)
    : environment_(std::move(environment)), output_layout_(output_layout),
      identity_(std::move(identity))
{
}
build_policy build_policy::make(environment_policy environment,
                                output_layout_kind output_layout)
{
  if (output_layout != output_layout_kind::package_root)
    invalid("unsupported build output layout");
  auto identity = policy_id(environment, output_layout);
  return build_policy(std::move(environment), output_layout,
                      std::move(identity));
}
const environment_policy& build_policy::environment() const noexcept
{ return environment_; }
output_layout_kind build_policy::output_layout() const noexcept
{ return output_layout_; }
const build_policy_identity& build_policy::identity() const noexcept
{ return identity_; }

struct build_request::impl final {
  impl(pkgresolve::selected_package subject_value,
       pkgsource::source_snapshot source_value,
       build_input_set inputs_value,
       architecture_binding architectures_value,
       build_policy policy_value,
       build_request_identity identity_value)
      : subject(std::move(subject_value)), source(std::move(source_value)),
        inputs(std::move(inputs_value)),
        architectures(std::move(architectures_value)),
        policy(std::move(policy_value)), identity(std::move(identity_value))
  {
  }
  pkgresolve::selected_package subject;
  pkgsource::source_snapshot source;
  build_input_set inputs;
  architecture_binding architectures;
  build_policy policy;
  build_request_identity identity;
};

build_request::build_request(std::shared_ptr<const impl> value)
    : impl_(std::move(value))
{
}
build_request::build_request(const build_request&) noexcept = default;
build_request::build_request(build_request&&) noexcept = default;
build_request& build_request::operator=(const build_request&) noexcept = default;
build_request& build_request::operator=(build_request&&) noexcept = default;
build_request::~build_request() = default;

build_request build_request::seal(
    const pkgresolve::resolution_result& resolution,
    pkgresolve::package_selection_identity subject_identity,
    build_policy policy)
{
  auto inputs = build_input_set::admit(resolution, subject_identity);
  const auto& subject = inputs.subject();
  const auto* candidate = subject.candidate();
  if (candidate == nullptr)
    invalid("build subject lost catalog source authority");
  auto source = candidate->source();
  auto architectures = architecture_binding::select(
      source.recipe().architectures(), subject.architectures().build(),
      subject.architectures().target());
  auto identity = request_id(source, inputs, architectures, policy);
  return build_request(std::make_shared<impl>(
      subject, std::move(source), std::move(inputs),
      std::move(architectures), std::move(policy), std::move(identity)));
}

const pkgresolve::selected_package& build_request::subject() const noexcept
{ return impl_->subject; }
const pkgsource::source_snapshot& build_request::source() const noexcept
{ return impl_->source; }
const pkgsource::package_release& build_request::release() const noexcept
{ return impl_->source.recipe().release(); }
const build_input_set& build_request::inputs() const noexcept
{ return impl_->inputs; }
const architecture_binding& build_request::architectures() const noexcept
{ return impl_->architectures; }
const std::vector<pkgsource::selected_profile>&
build_request::selected_profiles() const noexcept
{ return impl_->source.recipe().selected_build_profiles(); }
const pkgsource::program& build_request::build_program() const noexcept
{ return impl_->source.recipe().build_program(); }
const build_policy& build_request::policy() const noexcept
{ return impl_->policy; }
const build_request_identity& build_request::identity() const noexcept
{ return impl_->identity; }

} // namespace pkgbuild
