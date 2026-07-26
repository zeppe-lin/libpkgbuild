// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/request.h>

#include <libpkgbuild/error.h>

#include "identity_support.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace pkgbuild {
namespace {

[[noreturn]] void invalid(std::string message)
{
  throw error(error_code::invalid_request, std::move(message));
}

source_material_set_identity material_set_id(
    const pkgsource::source_snapshot& source,
    const std::vector<materialized_source>& materials)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/source-material-set/v1");
  writer.text(source.identity().hex());
  writer.number(materials.size());
  for (const auto& material : materials) {
    writer.text(material.declaration().local_name());
    writer.text(material.identity().hex());
  }
  return source_material_set_identity::from_sha256(writer.finish());
}

build_input_set_identity input_set_id(
    const pkgsource::source_snapshot& source,
    const std::vector<materialized_package_input>& inputs)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-input-set/v1");
  writer.text(source.identity().hex());
  writer.number(inputs.size());
  for (const auto& input : inputs) {
    writer.text(to_string(input.resolved().scope()));
    writer.text(input.resolved().declared_package().name());
    writer.text(input.resolved().identity().hex());
    writer.text(input.tree().hex());
  }
  return build_input_set_identity::from_sha256(writer.finish());
}

build_policy_identity policy_id(const environment_policy& environment,
                                output_layout_kind output_layout)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-policy/v1");
  writer.text(environment.identity().hex());
  writer.text(to_string(output_layout));
  return build_policy_identity::from_sha256(writer.finish());
}

build_request_identity request_id(
    const pkgsource::source_snapshot& source,
    const source_material_set& sources,
    const build_input_set& inputs,
    const architecture_binding& architectures,
    const build_policy& policy)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-request/v1");
  writer.text(source.identity().hex());
  writer.text(source.recipe().identity().hex());
  writer.text(source.recipe().release().identity().hex());
  writer.text(source.recipe().build_program().content_digest().hex());
  writer.text(sources.identity().hex());
  writer.text(inputs.identity().hex());
  writer.text(architectures.build().name());
  writer.text(architectures.target().name());
  writer.number(source.recipe().selected_build_profiles().size());
  for (const auto& profile : source.recipe().selected_build_profiles()) {
    writer.text(profile.profile().name());
    writer.text(profile.identity().hex());
  }
  writer.text(policy.identity().hex());
  return build_request_identity::from_sha256(writer.finish());
}

using requirement_key = std::pair<input_scope, std::string>;

std::set<requirement_key> expected_inputs(const pkgsource::source_snapshot& source)
{
  std::set<requirement_key> expected;
  for (const auto& requirement : source.recipe().build_requirements())
    expected.emplace(input_scope::build, requirement.package().name());
  for (const auto& requirement : source.recipe().check_requirements())
    expected.emplace(input_scope::check, requirement.package().name());
  return expected;
}

bool allowed(const std::vector<pkgsource::architecture_reference>& declared,
             const pkgsource::architecture_reference& selected)
{
  return declared.empty() ||
      std::find(declared.begin(), declared.end(), selected) != declared.end();
}

} // namespace

source_material_set::source_material_set(
    std::vector<materialized_source> materials,
    source_material_set_identity identity)
    : materials_(std::move(materials)), identity_(std::move(identity))
{
}

source_material_set source_material_set::seal(
    const pkgsource::source_snapshot& source,
    std::vector<materialized_source> materials)
{
  const auto& declared = source.recipe().sources();
  if (materials.size() != declared.size())
    invalid("source material set is incomplete or contains extra material");

  std::sort(materials.begin(), materials.end(),
            [](const materialized_source& lhs, const materialized_source& rhs) {
              return lhs.declaration().local_name() < rhs.declaration().local_name();
            });
  std::vector<pkgsource::source_input> expected = declared;
  std::sort(expected.begin(), expected.end(),
            [](const pkgsource::source_input& lhs, const pkgsource::source_input& rhs) {
              return lhs.local_name() < rhs.local_name();
            });

  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (materials[index].declaration() != expected[index])
      invalid("source material does not match the sealed source declaration: " +
              expected[index].local_name());
    if (index > 0 &&
        materials[index - 1].declaration().local_name() ==
            materials[index].declaration().local_name())
      invalid("source material set contains a duplicate local name");
  }
  auto identity = material_set_id(source, materials);
  return source_material_set(std::move(materials), std::move(identity));
}

const std::vector<materialized_source>& source_material_set::materials() const noexcept { return materials_; }
const source_material_set_identity& source_material_set::identity() const noexcept { return identity_; }

build_input_set::build_input_set(
    std::vector<materialized_package_input> inputs,
    build_input_set_identity identity)
    : inputs_(std::move(inputs)), identity_(std::move(identity))
{
}

build_input_set build_input_set::seal(
    const pkgsource::source_snapshot& source,
    std::vector<materialized_package_input> inputs)
{
  std::sort(inputs.begin(), inputs.end(),
            [](const materialized_package_input& lhs,
               const materialized_package_input& rhs) {
              return std::make_tuple(lhs.resolved().scope(),
                                     lhs.resolved().declared_package().name(),
                                     lhs.resolved().identity(), lhs.tree()) <
                     std::make_tuple(rhs.resolved().scope(),
                                     rhs.resolved().declared_package().name(),
                                     rhs.resolved().identity(), rhs.tree());
            });

  std::set<requirement_key> observed;
  for (const auto& input : inputs) {
    const requirement_key key{input.resolved().scope(),
                              input.resolved().declared_package().name()};
    if (!observed.insert(key).second)
      invalid("build input set contains a duplicate requirement binding");
  }
  if (observed != expected_inputs(source))
    invalid("build input set does not exactly satisfy sealed build/check requirements");

  auto identity = input_set_id(source, inputs);
  return build_input_set(std::move(inputs), std::move(identity));
}

const std::vector<materialized_package_input>& build_input_set::inputs() const noexcept { return inputs_; }
std::vector<materialized_package_input> build_input_set::for_scope(input_scope scope) const
{
  std::vector<materialized_package_input> result;
  for (const auto& input : inputs_) {
    if (input.resolved().scope() == scope)
      result.push_back(input);
  }
  return result;
}
const build_input_set_identity& build_input_set::identity() const noexcept { return identity_; }

architecture_binding::architecture_binding(
    std::vector<pkgsource::architecture_reference> declared_build,
    std::vector<pkgsource::architecture_reference> declared_target,
    pkgsource::architecture_reference build,
    pkgsource::architecture_reference target)
    : declared_build_(std::move(declared_build)),
      declared_target_(std::move(declared_target)),
      build_(std::move(build)), target_(std::move(target))
{
}

architecture_binding architecture_binding::select(
    const pkgsource::architecture_requirements& declared,
    pkgsource::architecture_reference build,
    pkgsource::architecture_reference target)
{
  if (!allowed(declared.build(), build))
    invalid("selected build architecture is not admitted by source authority");
  if (!allowed(declared.target(), target))
    invalid("selected target architecture is not admitted by source authority");
  return architecture_binding(declared.build(), declared.target(),
                              std::move(build), std::move(target));
}
const std::vector<pkgsource::architecture_reference>& architecture_binding::declared_build() const noexcept { return declared_build_; }
const std::vector<pkgsource::architecture_reference>& architecture_binding::declared_target() const noexcept { return declared_target_; }
const pkgsource::architecture_reference& architecture_binding::build() const noexcept { return build_; }
const pkgsource::architecture_reference& architecture_binding::target() const noexcept { return target_; }

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
  auto identity = policy_id(environment, output_layout);
  return build_policy(std::move(environment), output_layout,
                      std::move(identity));
}
const environment_policy& build_policy::environment() const noexcept { return environment_; }
output_layout_kind build_policy::output_layout() const noexcept { return output_layout_; }
const build_policy_identity& build_policy::identity() const noexcept { return identity_; }

build_request::build_request(pkgsource::source_snapshot source,
    source_material_set sources, build_input_set inputs,
    architecture_binding architectures, build_policy policy,
    build_request_identity identity)
    : source_(std::move(source)), sources_(std::move(sources)),
      inputs_(std::move(inputs)), architectures_(std::move(architectures)),
      policy_(std::move(policy)), identity_(std::move(identity))
{
}

build_request build_request::seal(
    pkgsource::source_snapshot source,
    std::vector<materialized_source> sources,
    std::vector<materialized_package_input> package_inputs,
    pkgsource::architecture_reference build_architecture,
    pkgsource::architecture_reference target_architecture,
    build_policy policy)
{
  auto source_set = source_material_set::seal(source, std::move(sources));
  auto input_set = build_input_set::seal(source, std::move(package_inputs));
  auto architectures = architecture_binding::select(
      source.recipe().architectures(), std::move(build_architecture),
      std::move(target_architecture));
  auto identity = request_id(source, source_set, input_set, architectures, policy);
  return build_request(std::move(source), std::move(source_set),
                       std::move(input_set), std::move(architectures),
                       std::move(policy), std::move(identity));
}

const pkgsource::source_snapshot& build_request::source() const noexcept { return source_; }
const pkgsource::package_release& build_request::release() const noexcept { return source_.recipe().release(); }
const source_material_set& build_request::sources() const noexcept { return sources_; }
const build_input_set& build_request::inputs() const noexcept { return inputs_; }
const architecture_binding& build_request::architectures() const noexcept { return architectures_; }
const std::vector<pkgsource::selected_profile>& build_request::selected_profiles() const noexcept { return source_.recipe().selected_build_profiles(); }
const pkgsource::program& build_request::build_program() const noexcept { return source_.recipe().build_program(); }
const build_policy& build_request::policy() const noexcept { return policy_; }
const build_request_identity& build_request::identity() const noexcept { return identity_; }

} // namespace pkgbuild
