// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../support/test.h"

#include <libpkgbuild/libpkgbuild.h>

int main()
{
  const auto environment =
      pkgbuild::environment_policy::hermetic(8, 0027, 1700000000);
  TEST_CHECK(environment.locale() == pkgbuild::locale_policy::c_utf8);
  TEST_CHECK(environment.timezone() == pkgbuild::timezone_policy::utc);
  TEST_CHECK(environment.network() == pkgbuild::network_policy::denied);
  TEST_CHECK(environment.home() == pkgbuild::home_policy::isolated);
  TEST_CHECK(environment.parallelism() == 8);
  TEST_CHECK(environment.file_creation_mask() == 0027);
  TEST_CHECK(environment.source_date_epoch() == 1700000000);

  const auto changed = pkgbuild::environment_policy::hermetic(9, 0027, 1700000000);
  TEST_CHECK(environment.identity() != changed.identity());
  const auto build_policy = pkgbuild::build_policy::make(environment);
  TEST_CHECK(build_policy.output_layout() == pkgbuild::output_layout_kind::package_root);
  TEST_CHECK(build_policy.environment() == environment);

  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_model,
                       pkgbuild::environment_policy::hermetic(0));
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_model,
                       pkgbuild::environment_policy::hermetic(1, 01000));
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_model,
                       pkgbuild::environment_policy::hermetic(1, 0022, -1));

  TEST_CHECK(pkgbuild::to_string(pkgbuild::input_scope::build) == "build");
  TEST_CHECK(pkgbuild::to_string(pkgbuild::input_scope::check) == "check");
  TEST_CHECK(pkgbuild::to_string(pkgbuild::output_layout_kind::package_root) ==
             "package-root");
  TEST_CHECK(pkgbuild::to_string(static_cast<pkgbuild::output_layout_kind>(99)) ==
             "unknown");
  TEST_PKGBUILD_THROWS(
      pkgbuild::error_code::invalid_request,
      pkgbuild::build_policy::make(
          environment, static_cast<pkgbuild::output_layout_kind>(99)));
  TEST_CHECK(pkgbuild::to_string(static_cast<pkgbuild::build_outcome>(99)) ==
             "unknown");
}
