#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
require() {
    file=$1
    text=$2
    grep -F -- "$text" "$file" >/dev/null || {
        echo "missing release metadata in ${file#$root/}: $text" >&2
        exit 1
    }
}
require "$root/meson.build" "  version: '1.0.0',"
require "$root/src/meson.build" "  soversion: '2',"
require "$root/adapter/meson.build" "  soversion: '1',"
require "$root/src/meson.build" "'libpkgsource >= 1.0.0'"
require "$root/adapter/meson.build" "'libpkgsource-plan >= 1.0.0'"
require "$root/adapter/meson.build" "'libpkgimage >= 0.3.0'"
require "$root/adapter/meson.build" "'libpkgplan >= 0.2.0'"
require "$root/HISTORY.md" '## 1.0.0'
require "$root/README.md" 'Version 1 is intentionally incompatible'
require "$root/DESIGN.md" 'libpkgstate 1.0.0'
require "$root/man/libpkgbuild.3.scdoc" 'Version 1 is an incompatible authority reset.'
