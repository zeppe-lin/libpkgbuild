// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/error.h>

#include <utility>

namespace pkgbuild {

error::error(error_code code, std::string message)
    : std::runtime_error(std::move(message)), code_(code)
{
}

error::~error() = default;

error_code error::code() const noexcept
{
  return code_;
}

} // namespace pkgbuild
