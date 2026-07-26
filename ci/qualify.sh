#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

srcdir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=${1:-"$srcdir/.qualification"}

command -v meson >/dev/null 2>&1 || {
    echo "meson is required" >&2
    exit 2
}

rm -rf "$work"
mkdir -p "$work"

qualified_compiler=
for compiler in g++ clang++; do
    command -v "$compiler" >/dev/null 2>&1 || continue
    [ -n "$qualified_compiler" ] || qualified_compiler=$compiler

    for library in shared static; do
        name=$(printf '%s-%s' "$compiler" "$library" | tr + _)
        build="$work/$name"
        install_root="$work/$name-install"

        CXX=$compiler meson setup "$build" "$srcdir" \
            --prefix=/usr \
            -Ddefault_library="$library" \
            -Dplanner_adapter=enabled \
            -Dparity=enabled \
            -Dmanpages=enabled \
            -Dwerror=true
        meson compile -C "$build"
        meson test -C "$build" --print-errorlogs

        rm -rf "$install_root"
        DESTDIR=$install_root meson install -C "$build"
        "$srcdir/tests/check_install_layout.sh" \
            "$install_root/usr/libexec/pkgbuild-pkgfile" \
            "$install_root/usr/libexec/pkgbuild-stage-scan" \
            "$srcdir/libpkgbuild/pkgbuild-pkgfile.in"
    done
done

[ -n "$qualified_compiler" ] || {
    echo 'no supported C++ compiler found' >&2
    exit 2
}

# The complete matrix qualifies composition.  These profiles prove that an
# explicit disable remains authoritative when the other optional surface uses
# some of the same dependencies.
for profile in core planner parity; do
    build="$work/features-$profile"
    case $profile in
    core)
        planner=disabled
        parity=disabled
        ;;
    planner)
        planner=enabled
        parity=disabled
        ;;
    parity)
        planner=disabled
        parity=enabled
        ;;
    esac

    CXX=$qualified_compiler meson setup "$build" "$srcdir" \
        --prefix=/usr \
        -Ddefault_library=shared \
        -Dplanner_adapter=$planner \
        -Dparity=$parity \
        -Dmanpages=disabled \
        -Dwerror=true
    meson compile -C "$build"
    meson test -C "$build" --print-errorlogs
done

"$srcdir/tests/check_meson_topology.sh" "$srcdir"
"$srcdir/tests/check_adapter_contract.sh" "$srcdir"
"$srcdir/tests/check_manpage_contract.sh" "$srcdir"
"$srcdir/tests/check_release_metadata.sh" "$srcdir"
