// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/libpkgbuild.h>

#include <cstddef>

#if !defined(__x86_64__)
#error "libpkgbuild 3.0 ABI layout qualification is x86-64 specific"
#endif

static_assert(sizeof(void*) == 8, "Zeppe-Lin x86-64 ABI qualification required");
static_assert(alignof(void*) == 8, "unexpected x86-64 pointer alignment");
static_assert(sizeof(pkgbuild::build_input) == 16);
static_assert(sizeof(pkgbuild::build_input_set) == 16);
static_assert(sizeof(pkgbuild::architecture_binding) == 16);
static_assert(sizeof(pkgbuild::build_request) == 16);
static_assert(sizeof(pkgbuild::build_result) == 16);
static_assert(sizeof(pkgbuild::environment_policy) == 56);
static_assert(sizeof(pkgbuild::build_policy) == 96);
static_assert(sizeof(pkgbuild::payload_entry) == 224);
static_assert(sizeof(pkgbuild::payload_manifest) == 56);
static_assert(sizeof(pkgbuild::sealed_artifact) == 80);

int main() { return 0; }
