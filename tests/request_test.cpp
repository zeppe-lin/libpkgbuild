// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.h"

#include <cassert>

int main()
{
  auto resolution = fixture::resolution();
  const auto& subject = fixture::subject(resolution);
  const auto first = pkgbuild::build_request::seal(
      resolution, subject.identity(), fixture::policy());
  const auto second = pkgbuild::build_request::seal(
      resolution, subject.identity(), fixture::policy());

  assert(first.identity() == second.identity());
  assert(first.subject().identity() == subject.identity());
  assert(first.source().identity() == subject.source_snapshot());
  assert(first.inputs().for_scope(pkgbuild::input_scope::build).size() == 2);
  assert(first.inputs().for_scope(pkgbuild::input_scope::check).size() == 1);
  assert(first.selected_profiles().size() == 1);
  assert(first.build_program().material().find("meson compile") !=
         std::string::npos);
  assert(first.architectures().build().name() == "x86_64");
  assert(first.architectures().target().name() == "x86_64");
  assert(sizeof(pkgbuild::build_request) == sizeof(void*) * 2U);
}
