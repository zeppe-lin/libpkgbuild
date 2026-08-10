#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=${1:?source root required}
fail() { echo "ci-contract: $*" >&2; exit 1; }
workflow=$root/.github/workflows/ci.yml
runner=$root/ci/configure-and-test.sh
[ -s "$workflow" ] || fail 'hosted CI workflow is absent'
[ -x "$runner" ] || fail 'qualification runner is absent or not executable'
for text in \
    'GCC shared' 'GCC static' 'Clang shared' 'Clang static' \
    'GCC release' 'address,undefined' \
    'libpkgsource' 'libpkgcatalog' 'libpkgstate' 'libpkgresolve'
do
    grep -F "$text" "$workflow" >/dev/null || fail "workflow omits $text"
done
for text in \
    'meson install -C "$build/product"' \
    'tests/installed/consumer.cpp' \
    'pkg-config --static --libs libpkgbuild' \
    'LD_LIBRARY_PATH='
do
    grep -F "$text" "$runner" >/dev/null || fail "runner omits installed-product gate: $text"
done
