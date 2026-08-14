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
[ "$(grep -c 'repository: zeppe-lin/libpkgsource, ref: v4.1.0' "$workflow")" -eq 2 ] ||
    fail 'workflow does not pin libpkgsource v4.1.0 in both matrices'
[ "$(grep -c 'repository: zeppe-lin/libpkgcatalog, ref: v4.0.0' "$workflow")" -eq 2 ] ||
    fail 'workflow does not pin libpkgcatalog v4.0.0 in both matrices'
[ "$(grep -c 'repository: zeppe-lin/libpkgresolve, ref: v4.0.0' "$workflow")" -eq 2 ] ||
    fail 'workflow does not pin libpkgresolve v4.0.0 in both matrices'
grep -F 'ref: f74df278b47b48e798c3de01c922c59b58319d13' "$workflow" >/dev/null ||
    fail 'workflow omits current libpkgstate authority revision'
! grep -F 'ref: f8786884cde0d2692119a79ac98582fade20fe97' "$workflow" >/dev/null ||
    fail 'stale resolver-3 authority revision remains'
for text in \
    'meson install -C "$build/product"' \
    'tests/installed/consumer.cpp' \
    'pkg-config --static --libs libpkgbuild' \
    'LD_LIBRARY_PATH='
do
    grep -F "$text" "$runner" >/dev/null || fail "runner omits installed-product gate: $text"
done
