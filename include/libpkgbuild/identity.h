// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file identity.h
 *  \brief Domain-separated SHA-256 build identities.
 */
#pragma once

#include <string>

namespace pkgbuild {

#define PKGBUILD_DECLARE_IDENTITY(type_name)                                   \
class type_name final {                                                        \
public:                                                                        \
  [[nodiscard]] static type_name from_sha256(std::string hex);                 \
  [[nodiscard]] const std::string& hex() const noexcept;                       \
  friend bool operator==(const type_name& lhs, const type_name& rhs) noexcept; \
  friend bool operator!=(const type_name& lhs, const type_name& rhs) noexcept; \
  friend bool operator<(const type_name& lhs, const type_name& rhs) noexcept;  \
private:                                                                       \
  explicit type_name(std::string hex);                                         \
  std::string hex_;                                                            \
}

PKGBUILD_DECLARE_IDENTITY(source_material_identity);
PKGBUILD_DECLARE_IDENTITY(source_material_set_identity);
PKGBUILD_DECLARE_IDENTITY(resolved_package_input_identity);
PKGBUILD_DECLARE_IDENTITY(input_tree_identity);
PKGBUILD_DECLARE_IDENTITY(build_input_set_identity);
PKGBUILD_DECLARE_IDENTITY(environment_policy_identity);
PKGBUILD_DECLARE_IDENTITY(build_policy_identity);
PKGBUILD_DECLARE_IDENTITY(build_request_identity);
PKGBUILD_DECLARE_IDENTITY(payload_manifest_identity);
PKGBUILD_DECLARE_IDENTITY(artifact_identity);
PKGBUILD_DECLARE_IDENTITY(artifact_binding_identity);
PKGBUILD_DECLARE_IDENTITY(execution_evidence_identity);
PKGBUILD_DECLARE_IDENTITY(failure_evidence_identity);
PKGBUILD_DECLARE_IDENTITY(build_result_identity);

#undef PKGBUILD_DECLARE_IDENTITY

} // namespace pkgbuild
