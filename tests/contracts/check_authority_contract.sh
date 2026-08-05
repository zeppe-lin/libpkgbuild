#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
surface="$root/include $root/src $root/meson.build $root/meson_options.txt"
for forbidden in \
    materialized_source source_material_set source_material_identity \
    source_material_set_identity materialized_package_input input_tree_identity \
    resolved_package_input archive_backend libpkgbuild-plan \
    package_root_v1 package_tar_v1 Pkgfile pkgfile fakeroot md5 MD5 \
    LegacyBuildReceipt strip_exclusions footprint parity BuildDirectory \
    workspace_directory 'map<std::string, std::string>'
do
    if grep -R -F -- "$forbidden" $surface >/dev/null 2>&1; then
        echo "forbidden authority in libpkgbuild core: $forbidden" >&2
        exit 1
    fi
done

grep -F "'libpkgsource'," "$root/meson.build" >/dev/null
grep -F "version: ['>=3.0.0', '<4.0.0']" "$root/meson.build" >/dev/null
grep -F "'libpkgresolve'," "$root/meson.build" >/dev/null
grep -F "version: ['>=2.0.0', '<3.0.0']" "$root/meson.build" >/dev/null
grep -F 'pkgresolve::resolution_result' "$root/include/libpkgbuild/request.h" >/dev/null
grep -F 'std::shared_ptr<const impl> impl_' "$root/include/libpkgbuild/request.h" >/dev/null
grep -F 'std::shared_ptr<const impl> impl_' "$root/include/libpkgbuild/result.h" >/dev/null
grep -F 'libpkgbuild/build-input/1' "$root/src/request.cpp" >/dev/null
grep -F 'libpkgbuild/build-request/1' "$root/src/request.cpp" >/dev/null
grep -F 'libpkgbuild/build-result/1' "$root/src/result.cpp" >/dev/null
