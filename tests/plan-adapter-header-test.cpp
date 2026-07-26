// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later

#include <pkgbuild-plan/adapter.hpp>

int main()
{
    static_assert(static_cast<int>(
        pkgbuild::plan_adapter::projection_error_code::build_receipt) == 0);
    return 0;
}
