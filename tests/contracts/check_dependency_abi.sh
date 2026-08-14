#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
library=${1:?shared library required}
fail() { echo "dependency-abi-test: $*" >&2; exit 1; }
command -v readelf >/dev/null 2>&1 || fail 'readelf is required'
needed=$(readelf -d "$library" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
for expected in libpkgsource.so.4 libpkgresolve.so.3; do
    printf '%s\n' "$needed" | grep -Fx "$expected" >/dev/null ||
        fail "missing direct NEEDED $expected"
done
for obsolete in \
    libpkgsource.so.1 libpkgsource.so.2 libpkgsource.so.3 \
    libpkgresolve.so.1 libpkgresolve.so.2
do
    ! printf '%s\n' "$needed" | grep -Fx "$obsolete" >/dev/null ||
        fail "obsolete provider generation admitted: $obsolete"
done
