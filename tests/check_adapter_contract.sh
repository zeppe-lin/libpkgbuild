#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
srcdir=${1:-.}

require() {
    grep -F "$2" "$srcdir/$1" >/dev/null || {
        echo "$1: missing contract: $2" >&2
        exit 1
    }
}
reject() {
    if grep -F "$2" "$srcdir/$1" >/dev/null; then
        echo "$1: forbidden core dependency: $2" >&2
        exit 1
    fi
}

require meson_options.txt "'planner_adapter'"
require adapter/meson.build "'libpkgsource-plan >= 0.2.0'"
require adapter/meson.build "'libpkgimage >= 0.3.0'"
require adapter/meson.build "'libpkgplan >= 0.2.0'"
require include/pkgbuild-plan/adapter.hpp "project_artifact("
require include/pkgbuild/definition.hpp "SealedArtifactReceipt"

core_block=$(sed -n '/pkgconfig.generate(/,/^)/p' "$srcdir/meson.build")
printf '%s\n' "$core_block" | grep -F "libpkgsource >= 0.1.0" >/dev/null
printf '%s\n' "$core_block" | grep -F 'libpkgplan' >/dev/null && exit 1
printf '%s\n' "$core_block" | grep -F 'libpkgimage' >/dev/null && exit 1

reject include/pkgbuild/definition.hpp '<libpkgplan/'
reject include/pkgbuild/definition.hpp '<libpkgimage/'
