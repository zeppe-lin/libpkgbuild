// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.h"

#include <algorithm>
#include <cassert>
#include <functional>

namespace {

template<typename Function>
void reject(Function&& function)
{
  try {
    function();
    assert(false);
  } catch (const pkgbuild::error& value) {
    assert(value.code() == pkgbuild::error_code::invalid_request);
  }
}

void test_complete_request()
{
  auto first_source = fixture::source();
  auto first_materials = fixture::materials(first_source);
  auto first_inputs = fixture::inputs();
  auto first = pkgbuild::build_request::seal(
      first_source, first_materials, first_inputs,
      pkgsource::architecture_reference("x86_64"),
      pkgsource::architecture_reference("x86_64"), fixture::policy());

  std::reverse(first_materials.begin(), first_materials.end());
  std::reverse(first_inputs.begin(), first_inputs.end());
  auto second_source = fixture::source();
  auto second = pkgbuild::build_request::seal(
      second_source, first_materials, first_inputs,
      pkgsource::architecture_reference("x86_64"),
      pkgsource::architecture_reference("x86_64"), fixture::policy());

  assert(first.identity() == second.identity());
  assert(first.inputs().for_scope(pkgbuild::input_scope::build).size() == 2);
  assert(first.inputs().for_scope(pkgbuild::input_scope::check).size() == 1);
  assert(first.selected_profiles().size() == 1);
  assert(first.build_program().material().find("meson compile") != std::string::npos);
}

void test_exact_admission()
{
  reject([] {
    auto source = fixture::source();
    auto materials = fixture::materials(source);
    materials.pop_back();
    (void)pkgbuild::build_request::seal(
        source, std::move(materials), fixture::inputs(),
        pkgsource::architecture_reference("x86_64"),
        pkgsource::architecture_reference("x86_64"), fixture::policy());
  });
  reject([] {
    auto source = fixture::source();
    auto inputs = fixture::inputs();
    inputs.pop_back();
    (void)pkgbuild::build_request::seal(
        source, fixture::materials(source), std::move(inputs),
        pkgsource::architecture_reference("x86_64"),
        pkgsource::architecture_reference("x86_64"), fixture::policy());
  });
  reject([] {
    auto source = fixture::source();
    (void)pkgbuild::build_request::seal(
        source, fixture::materials(source), fixture::inputs(),
        pkgsource::architecture_reference("aarch64"),
        pkgsource::architecture_reference("x86_64"), fixture::policy());
  });
}

} // namespace

int main()
{
  test_complete_request();
  test_exact_admission();
}
