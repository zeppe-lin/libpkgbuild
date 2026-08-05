// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/** @file result.h
 *  @brief Sealed native package-build outcomes.
 */
#pragma once

#include <memory>
#include <optional>

#include <libpkgbuild/request.h>

namespace pkgbuild {

/** @brief Complete build outcome bound to its exact request and evidence. */
class PKGBUILD_API build_result final {
public:
  build_result(const build_result&) noexcept;
  build_result(build_result&&) noexcept;
  build_result& operator=(const build_result&) noexcept;
  build_result& operator=(build_result&&) noexcept;
  ~build_result();

  [[nodiscard]] static build_result succeeded(
      build_request request,
      payload_manifest payload,
      sealed_artifact artifact,
      execution_evidence_identity execution_evidence);
  [[nodiscard]] static build_result failed(
      build_request request,
      execution_evidence_identity execution_evidence,
      failure_evidence_identity failure_evidence);

  [[nodiscard]] build_outcome outcome() const noexcept;
  [[nodiscard]] const build_request& request() const noexcept;
  [[nodiscard]] const execution_evidence_identity&
  execution_evidence() const noexcept;
  [[nodiscard]] const std::optional<payload_manifest>& payload() const noexcept;
  [[nodiscard]] const std::optional<sealed_artifact>& artifact() const noexcept;
  [[nodiscard]] const std::optional<artifact_binding_identity>&
  artifact_binding() const noexcept;
  [[nodiscard]] const std::optional<failure_evidence_identity>&
  failure_evidence() const noexcept;
  [[nodiscard]] const build_result_identity& identity() const noexcept;

  friend PKGBUILD_API bool operator==(const build_result& lhs,
                                      const build_result& rhs) noexcept;
  friend PKGBUILD_API bool operator!=(const build_result& lhs,
                                      const build_result& rhs) noexcept;

private:
  struct impl;
  explicit build_result(std::shared_ptr<const impl> value);
  std::shared_ptr<const impl> impl_;
};

} // namespace pkgbuild
