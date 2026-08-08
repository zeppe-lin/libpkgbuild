// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../fixtures/build.h"
#include "../support/test.h"

int main()
{
  using pkgsource::architecture_reference;
  using pkgsource::architecture_requirements;

  const auto open = pkgbuild::architecture_binding::select(
      architecture_requirements({}, {}), architecture_reference("x86_64"),
      architecture_reference("aarch64"));
  TEST_CHECK(open.build().name() == "x86_64");
  TEST_CHECK(open.target().name() == "aarch64");

  const auto constrained = pkgbuild::architecture_binding::select(
      architecture_requirements(
          {architecture_reference("x86_64")},
          {architecture_reference("aarch64")}),
      architecture_reference("x86_64"), architecture_reference("aarch64"));
  TEST_CHECK(constrained.target().name() == "aarch64");

  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::architecture_binding::select(
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("aarch64")}),
          architecture_reference("riscv64"), architecture_reference("aarch64")));
  TEST_PKGBUILD_THROWS(pkgbuild::error_code::invalid_request,
      pkgbuild::architecture_binding::select(
          architecture_requirements(
              {architecture_reference("x86_64")},
              {architecture_reference("aarch64")}),
          architecture_reference("x86_64"), architecture_reference("riscv64")));

  auto resolved = fixture::resolution({}, {}, "x86_64", "aarch64");
  const auto request = pkgbuild::build_request::seal(
      resolved, fixture::subject(resolved).identity(), fixture::policy());
  TEST_CHECK(request.architectures().build().name() == "x86_64");
  TEST_CHECK(request.architectures().target().name() == "aarch64");
}
