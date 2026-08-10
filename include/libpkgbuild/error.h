// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

/*! \file error.h
 *  \brief Native build-authority failures.
 */
#pragma once

#include <libpkgbuild/export.h>

#include <stdexcept>
#include <string>

namespace pkgbuild {

enum class error_code {
  invalid_identity,
  identity_failed,
  invalid_model,
  invalid_request,
  invalid_result,
};

class PKGBUILD_API error final : public std::runtime_error {
public:
  error(error_code code, std::string message);
  ~error() override;
  [[nodiscard]] error_code code() const noexcept;
private:
  error_code code_;
};

} // namespace pkgbuild
