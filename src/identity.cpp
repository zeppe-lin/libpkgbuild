// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/identity.h>

#include "identity_support.h"

#include <utility>

namespace pkgbuild {

#define PKGBUILD_DEFINE_IDENTITY(type_name)                                    \
type_name::type_name(std::string hex) : hex_(std::move(hex)) {}                \
type_name type_name::from_sha256(std::string hex) {                            \
  detail::require_sha256_hex(hex);                                              \
  return type_name(std::move(hex));                                             \
}                                                                               \
const std::string& type_name::hex() const noexcept { return hex_; }             \
bool operator==(const type_name& lhs, const type_name& rhs) noexcept {          \
  return lhs.hex_ == rhs.hex_;                                                   \
}                                                                               \
bool operator!=(const type_name& lhs, const type_name& rhs) noexcept {          \
  return !(lhs == rhs);                                                          \
}                                                                               \
bool operator<(const type_name& lhs, const type_name& rhs) noexcept {           \
  return lhs.hex_ < rhs.hex_;                                                    \
}

PKGBUILD_DEFINE_IDENTITY(build_input_identity)
PKGBUILD_DEFINE_IDENTITY(build_input_set_identity)
PKGBUILD_DEFINE_IDENTITY(environment_policy_identity)
PKGBUILD_DEFINE_IDENTITY(build_policy_identity)
PKGBUILD_DEFINE_IDENTITY(build_request_identity)
PKGBUILD_DEFINE_IDENTITY(payload_manifest_identity)
PKGBUILD_DEFINE_IDENTITY(artifact_identity)
PKGBUILD_DEFINE_IDENTITY(artifact_binding_identity)
PKGBUILD_DEFINE_IDENTITY(execution_evidence_identity)
PKGBUILD_DEFINE_IDENTITY(failure_evidence_identity)
PKGBUILD_DEFINE_IDENTITY(build_result_identity)

#undef PKGBUILD_DEFINE_IDENTITY

} // namespace pkgbuild
