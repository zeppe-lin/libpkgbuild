// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file result.h
 *  \brief Sealed native build outcomes.
 */
#pragma once

#include <optional>

#include <libpkgbuild/request.h>

namespace pkgbuild {

/*! \brief Complete build outcome bound to its exact request and evidence. */
class build_result final {
public:
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
  [[nodiscard]] const execution_evidence_identity& execution_evidence() const noexcept;
  [[nodiscard]] const std::optional<payload_manifest>& payload() const noexcept;
  [[nodiscard]] const std::optional<sealed_artifact>& artifact() const noexcept;
  [[nodiscard]] const std::optional<artifact_binding_identity>& artifact_binding() const noexcept;
  [[nodiscard]] const std::optional<failure_evidence_identity>& failure_evidence() const noexcept;
  [[nodiscard]] const build_result_identity& identity() const noexcept;

  friend bool operator==(const build_result& lhs,
                         const build_result& rhs) noexcept;
  friend bool operator!=(const build_result& lhs,
                         const build_result& rhs) noexcept;
private:
  build_result(build_outcome outcome,
               build_request request,
               execution_evidence_identity execution_evidence,
               std::optional<payload_manifest> payload,
               std::optional<sealed_artifact> artifact,
               std::optional<artifact_binding_identity> artifact_binding,
               std::optional<failure_evidence_identity> failure_evidence,
               build_result_identity identity);

  build_outcome outcome_;
  build_request request_;
  execution_evidence_identity execution_evidence_;
  std::optional<payload_manifest> payload_;
  std::optional<sealed_artifact> artifact_;
  std::optional<artifact_binding_identity> artifact_binding_;
  std::optional<failure_evidence_identity> failure_evidence_;
  build_result_identity identity_;
};

} // namespace pkgbuild
