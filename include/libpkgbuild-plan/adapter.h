// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file adapter.h
 *  \brief Native libpkgbuild artifact projection into libpkgplan.
 */
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

#include <libpkgbuild/result.h>
#include <libpkgimage/archive_backend.h>
#include <libpkgimage/inspection_receipt.h>
#include <libpkgplan/package_fact.h>
#include <libpkgsource-plan/adapter.h>

namespace pkgbuild::plan_adapter {

enum class projection_error_code {
  build_result,
  source_projection,
  archive_inspection,
  payload_mismatch,
  planner_fact,
};

class projection_error final : public std::runtime_error {
public:
  projection_error(projection_error_code code, std::string message);
  [[nodiscard]] projection_error_code code() const noexcept;
private:
  projection_error_code code_;
};

/*! \brief Planner facts bound to one verified successful build result. */
class artifact_projection final {
public:
  artifact_projection(
      pkgbuild::build_result build,
      pkgsource::plan_adapter::candidate_projection candidate,
      pkgimage::inspected_package_image image,
      pkgplan::artifact_package_fact artifact);
  [[nodiscard]] const pkgbuild::build_result& build() const noexcept;
  [[nodiscard]] const pkgsource::plan_adapter::candidate_projection&
  candidate() const noexcept;
  [[nodiscard]] const pkgimage::inspected_package_image& image() const noexcept;
  [[nodiscard]] const pkgplan::artifact_package_fact& artifact() const noexcept;
private:
  pkgbuild::build_result build_;
  pkgsource::plan_adapter::candidate_projection candidate_;
  pkgimage::inspected_package_image image_;
  pkgplan::artifact_package_fact artifact_;
};

/*! \brief Inspect exact artifact bytes and project verified planner facts.
 *
 * The pathname is transport only. The build result's exact digest and byte
 * count authenticate the retained bytes before planner admission.
 */
[[nodiscard]] artifact_projection project_artifact(
    pkgbuild::build_result build,
    const std::filesystem::path& artifact_path,
    const pkgimage::archive_backend& archives);

} // namespace pkgbuild::plan_adapter
