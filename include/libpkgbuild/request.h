// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file request.h
 *  \brief Sealed native build requests.
 */
#pragma once

#include <vector>

#include <libpkgbuild/model.h>

namespace pkgbuild {

/*! \brief Complete source materialization for one source snapshot. */
class source_material_set final {
public:
  [[nodiscard]] static source_material_set seal(
      const pkgsource::source_snapshot& source,
      std::vector<materialized_source> materials);
  [[nodiscard]] const std::vector<materialized_source>& materials() const noexcept;
  [[nodiscard]] const source_material_set_identity& identity() const noexcept;
private:
  source_material_set(std::vector<materialized_source> materials,
                      source_material_set_identity identity);
  std::vector<materialized_source> materials_;
  source_material_set_identity identity_;
};

/*! \brief Exact build/check requirements and their materialized trees. */
class build_input_set final {
public:
  [[nodiscard]] static build_input_set seal(
      const pkgsource::source_snapshot& source,
      std::vector<materialized_package_input> inputs);
  [[nodiscard]] const std::vector<materialized_package_input>& inputs() const noexcept;
  [[nodiscard]] std::vector<materialized_package_input> for_scope(
      input_scope scope) const;
  [[nodiscard]] const build_input_set_identity& identity() const noexcept;
private:
  build_input_set(std::vector<materialized_package_input> inputs,
                  build_input_set_identity identity);
  std::vector<materialized_package_input> inputs_;
  build_input_set_identity identity_;
};

/*! \brief Selected build and target architectures validated against source. */
class architecture_binding final {
public:
  [[nodiscard]] static architecture_binding select(
      const pkgsource::architecture_requirements& declared,
      pkgsource::architecture_reference build,
      pkgsource::architecture_reference target);
  [[nodiscard]] const std::vector<pkgsource::architecture_reference>&
  declared_build() const noexcept;
  [[nodiscard]] const std::vector<pkgsource::architecture_reference>&
  declared_target() const noexcept;
  [[nodiscard]] const pkgsource::architecture_reference& build() const noexcept;
  [[nodiscard]] const pkgsource::architecture_reference& target() const noexcept;
private:
  architecture_binding(
      std::vector<pkgsource::architecture_reference> declared_build,
      std::vector<pkgsource::architecture_reference> declared_target,
      pkgsource::architecture_reference build,
      pkgsource::architecture_reference target);
  std::vector<pkgsource::architecture_reference> declared_build_;
  std::vector<pkgsource::architecture_reference> declared_target_;
  pkgsource::architecture_reference build_;
  pkgsource::architecture_reference target_;
};

/*! \brief Closed caller policy accepted for one build realization. */
class build_policy final {
public:
  [[nodiscard]] static build_policy make(
      environment_policy environment,
      output_layout_kind output_layout = output_layout_kind::package_root_v1);
  [[nodiscard]] const environment_policy& environment() const noexcept;
  [[nodiscard]] output_layout_kind output_layout() const noexcept;
  [[nodiscard]] const build_policy_identity& identity() const noexcept;
private:
  build_policy(environment_policy environment,
               output_layout_kind output_layout,
               build_policy_identity identity);
  environment_policy environment_;
  output_layout_kind output_layout_;
  build_policy_identity identity_;
};

/*! \brief Complete immutable authority required before execution starts. */
class build_request final {
public:
  [[nodiscard]] static build_request seal(
      pkgsource::source_snapshot source,
      std::vector<materialized_source> sources,
      std::vector<materialized_package_input> package_inputs,
      pkgsource::architecture_reference build_architecture,
      pkgsource::architecture_reference target_architecture,
      build_policy policy);
  [[nodiscard]] const pkgsource::source_snapshot& source() const noexcept;
  [[nodiscard]] const pkgsource::package_release& release() const noexcept;
  [[nodiscard]] const source_material_set& sources() const noexcept;
  [[nodiscard]] const build_input_set& inputs() const noexcept;
  [[nodiscard]] const architecture_binding& architectures() const noexcept;
  [[nodiscard]] const std::vector<pkgsource::selected_profile>&
  selected_profiles() const noexcept;
  [[nodiscard]] const pkgsource::program& build_program() const noexcept;
  [[nodiscard]] const build_policy& policy() const noexcept;
  [[nodiscard]] const build_request_identity& identity() const noexcept;
private:
  build_request(pkgsource::source_snapshot source,
                source_material_set sources,
                build_input_set inputs,
                architecture_binding architectures,
                build_policy policy,
                build_request_identity identity);
  pkgsource::source_snapshot source_;
  source_material_set sources_;
  build_input_set inputs_;
  architecture_binding architectures_;
  build_policy policy_;
  build_request_identity identity_;
};

} // namespace pkgbuild
