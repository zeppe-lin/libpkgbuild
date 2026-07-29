#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
surface="$root/include $root/src $root/adapter $root/meson.build $root/meson_options.txt"
for forbidden in \
    Pkgfile pkgfile fakeroot md5 MD5 LegacyBuildReceipt legacy_32bit \
    strip_exclusions footprint parity BuildDirectory workspace_directory \
    'map<std::string, std::string>'
do
    if grep -R -F -- "$forbidden" $surface >/dev/null 2>&1; then
        echo "forbidden compatibility concept in native authority: $forbidden" >&2
        exit 1
    fi
done

grep -F 'libpkgsource >= 2.0.0' "$root/src/meson.build" >/dev/null
grep -F 'libpkgbuild/source-material/v1' "$root/src/model.cpp" >/dev/null
grep -F 'libpkgbuild/build-request/v1' "$root/src/request.cpp" >/dev/null
grep -F 'libpkgbuild/build-result/v1' "$root/src/result.cpp" >/dev/null
grep -F 'libpkgbuild-plan/artifact-manifest/v2' "$root/adapter/adapter.cpp" >/dev/null
