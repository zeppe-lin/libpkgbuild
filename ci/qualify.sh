#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=${1:-"$srcdir/.qualification"}
command -v meson >/dev/null 2>&1 || {
    echo 'meson is required' >&2
    exit 2
}
rm -rf "$work"
mkdir -p "$work"

for compiler in g++ clang++; do
    command -v "$compiler" >/dev/null 2>&1 || continue
    for mode in shared static; do
        build="$work/$(printf '%s-%s' "$compiler" "$mode" | tr + _)"
        CXX=$compiler meson setup "$build" "$srcdir" \
            -Ddefault_library="$mode" \
            -Dlink_mode="$mode" \
            -Dplanner_adapter=enabled \
            -Dman_pages=enabled \
            -Dwerror=true
        meson compile -C "$build"
        meson test -C "$build" --print-errorlogs
    done
done

"$srcdir/tests/check_authority_contract.sh" "$srcdir"
"$srcdir/tests/check_release_metadata.sh" "$srcdir"
"$srcdir/tests/check_manpage_contract.sh" "$srcdir"
