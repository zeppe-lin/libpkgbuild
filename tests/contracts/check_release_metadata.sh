#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
require()
{
    file=$1
    text=$2
    grep -F -- "$text" "$file" >/dev/null || {
        echo "missing release metadata in ${file#$root/}: $text" >&2
        exit 1
    }
}
require "$root/meson.build" "  version: '3.0.0',"
require "$root/src/meson.build" "  soversion: '4',"
require "$root/src/meson.build" "'libpkgsource >= 3.0.0'"
require "$root/src/meson.build" "'libpkgsource < 4.0.0'"
require "$root/src/meson.build" "'libpkgresolve >= 2.0.0'"
require "$root/src/meson.build" "'libpkgresolve < 3.0.0'"
require "$root/src/meson.build" "gnu_symbol_visibility: 'hidden'"
require "$root/HISTORY.md" '## 3.0.0'
require "$root/HISTORY.md" 'Core ABI: libpkgbuild.so.4.'
require "$root/README.md" 'opaque immutable storage'
