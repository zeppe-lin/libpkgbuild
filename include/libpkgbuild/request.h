// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/** @file request.h
 *  @brief Sealed logical package-build requests.
 */
#pragma once

#include <memory>
#include <vector>

#include <libpkgbuild/model.h>
#include <libpkgsource/snapshot.h>

namespace pkgbuild {

/** @brief Exact direct build/check resolver authority for one selected package. */
class PKGBUILD_API build_input_set final {
public:
  build_input_set(const build_input_set&) noexcept;
  build_input_set(build_input_set&&) noexcept;
  build_input_set& operator=(const build_input_set&) noexcept;
  build_input_set& operator=(build_input_set&&) noexcept;
  ~build_input_set();

  [[nodiscard]] static build_input_set admit(
      const pkgresolve::resolution_result& resolution,
      const pkgresolve::package_selection_identity& subject);

  [[nodiscard]] const pkgresolve::selected_package& subject() const noexcept;
  [[nodiscard]] const pkgresolve::resolution_result_identity&
  resolution() const noexcept;
  [[nodiscard]] const std::vector<build_input>& inputs() const noexcept;
  [[nodiscard]] std::vector<build_input> for_scope(input_scope scope) const;
  [[nodiscard]] const build_input_set_identity& identity() const noexcept;

private:
  struct impl;
  explicit build_input_set(std::shared_ptr<const impl> value);
  std::shared_ptr<const impl> impl_;
};

/** @brief Selected build and target architectures validated against source. */
class PKGBUILD_API architecture_binding final {
public:
  architecture_binding(const architecture_binding&) noexcept;
  architecture_binding(architecture_binding&&) noexcept;
  architecture_binding& operator=(const architecture_binding&) noexcept;
  architecture_binding& operator=(architecture_binding&&) noexcept;
  ~architecture_binding();

  [[nodiscard]] static architecture_binding select(
      const pkgsource::architecture_requirements& declared,
      pkgsource::architecture_reference build,
      pkgsource::architecture_reference target);
  [[nodiscard]] const pkgsource::architecture_reference& build() const noexcept;
  [[nodiscard]] const pkgsource::architecture_reference& target() const noexcept;

private:
  struct impl;
  explicit architecture_binding(std::shared_ptr<const impl> value);
  std::shared_ptr<const impl> impl_;
};

/** @brief Closed caller policy accepted for one build realization. */
class PKGBUILD_API build_policy final {
public:
  [[nodiscard]] static build_policy make(
      environment_policy environment,
      output_layout_kind output_layout = output_layout_kind::package_root);
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

/** @brief Complete immutable logical authority required before execution starts. */
class PKGBUILD_API build_request final {
public:
  build_request(const build_request&) noexcept;
  build_request(build_request&&) noexcept;
  build_request& operator=(const build_request&) noexcept;
  build_request& operator=(build_request&&) noexcept;
  ~build_request();

  [[nodiscard]] static build_request seal(
      const pkgresolve::resolution_result& resolution,
      pkgresolve::package_selection_identity subject,
      build_policy policy);

  [[nodiscard]] const pkgresolve::selected_package& subject() const noexcept;
  [[nodiscard]] const pkgsource::source_snapshot& source() const noexcept;
  [[nodiscard]] const pkgsource::package_release& release() const noexcept;
  [[nodiscard]] const build_input_set& inputs() const noexcept;
  [[nodiscard]] const architecture_binding& architectures() const noexcept;
  [[nodiscard]] const std::vector<pkgsource::selected_profile>&
  selected_profiles() const noexcept;
  [[nodiscard]] const pkgsource::program& build_program() const noexcept;
  [[nodiscard]] const build_policy& policy() const noexcept;
  [[nodiscard]] const build_request_identity& identity() const noexcept;

private:
  struct impl;
  explicit build_request(std::shared_ptr<const impl> value);
  std::shared_ptr<const impl> impl_;
};

} // namespace pkgbuild
