// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>

#include <pkgbuild/definition.hpp>

#include <libpkgimage/archive_backend.h>
#include <libpkgimage/inspection_receipt.h>
#include <libpkgplan/package_fact.h>
#include <libpkgsource-plan/adapter.h>

namespace pkgbuild::plan_adapter {

enum class projection_error_code {
    build_receipt,
    source_projection,
    archive_inspection,
    planner_fact,
};

class projection_error final : public std::runtime_error {
public:
    projection_error(projection_error_code code, std::string message);
    [[nodiscard]] projection_error_code code() const noexcept;

private:
    projection_error_code code_;
};

class artifact_projection final {
public:
    artifact_projection(
        pkgbuild::BuildReceipt build,
        pkgsource::plan_adapter::candidate_projection candidate,
        pkgimage::inspected_package_image image,
        pkgplan::artifact_package_fact artifact);

    [[nodiscard]] const pkgbuild::BuildReceipt& build() const noexcept;
    [[nodiscard]] const pkgsource::plan_adapter::candidate_projection&
    candidate() const noexcept;
    [[nodiscard]] const pkgimage::inspected_package_image& image() const noexcept;
    [[nodiscard]] const pkgplan::artifact_package_fact& artifact() const noexcept;

private:
    pkgbuild::BuildReceipt build_;
    pkgsource::plan_adapter::candidate_projection candidate_;
    pkgimage::inspected_package_image image_;
    pkgplan::artifact_package_fact artifact_;
};

[[nodiscard]] artifact_projection project_artifact(
    pkgbuild::BuildReceipt build,
    const pkgimage::archive_backend& archives);

} // namespace pkgbuild::plan_adapter
