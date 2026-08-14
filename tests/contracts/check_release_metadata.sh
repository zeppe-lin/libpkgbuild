#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
fail() { echo "release-metadata-test: $*" >&2; exit 1; }
require()
{
    file=$1
    text=$2
    grep -F -- "$text" "$file" >/dev/null ||
        fail "missing in ${file#$root/}: $text"
}
require_dependency_range()
{
    variable=$1
    package=$2
    range=$3
    block=$(sed -n "/^${variable} = dependency(/,/^)/p" "$root/meson.build")
    printf '%s\n' "$block" | grep -F "  '$package'," >/dev/null ||
        fail "$variable does not name $package"
    printf '%s\n' "$block" | grep -F "  version: $range," >/dev/null ||
        fail "$variable does not require $range"
}
require "$root/meson.build" "  version: '3.0.0',"
require_dependency_range libpkgsource_dep libpkgsource "['>=4.0.0', '<5.0.0']"
require_dependency_range libpkgresolve_dep libpkgresolve "['>=3.0.0', '<4.0.0']"
require "$root/src/meson.build" "  soversion: '4',"
require "$root/src/meson.build" 'requires: [libpkgsource_dep, libpkgresolve_dep]'
require "$root/src/meson.build" 'requires_private: [libcrypto_dep]'
require "$root/src/meson.build" "gnu_symbol_visibility: 'hidden'"
require "$root/HISTORY.md" '## 3.0.0'
require "$root/HISTORY.md" 'Core ABI: libpkgbuild.so.4.'
require "$root/HISTORY.md" 'libpkgresolve 3'
require "$root/README.md" 'opaque immutable storage'
require "$root/MAINTAINING.md" '219-symbol'
