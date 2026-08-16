#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
surface="$root/include $root/src $root/meson.build $root/meson.options"
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

model="$root/include/libpkgbuild/model.h"
if grep -F 'file_creation_mask = 0022' "$model" >/dev/null || \
   grep -F 'source_date_epoch = std::nullopt' "$model" >/dev/null; then
    echo 'environment policy constructor regained hidden caller defaults' >&2
    exit 1
fi
for required in \
    'std::uint32_t file_creation_mask,' \
    'std::optional<std::int64_t> source_date_epoch);'; do
    grep -F "$required" "$model" >/dev/null || {
        echo "environment policy constructor omits explicit dimension: $required" >&2
        exit 1
    }
done

source_block=$(sed -n '/^libpkgsource_dep = dependency(/,/^)/p' "$root/meson.build")
printf '%s\n' "$source_block" | grep -F "  'libpkgsource'," >/dev/null
printf '%s\n' "$source_block" | grep -F "  version: ['>=4.0.0', '<5.0.0']," >/dev/null
resolve_block=$(sed -n '/^libpkgresolve_dep = dependency(/,/^)/p' "$root/meson.build")
printf '%s\n' "$resolve_block" | grep -F "  'libpkgresolve'," >/dev/null
printf '%s\n' "$resolve_block" | grep -F "  version: ['>=4.0.0', '<5.0.0']," >/dev/null
grep -F 'pkgresolve::resolution_result' "$root/include/libpkgbuild/request.h" >/dev/null
grep -F 'std::shared_ptr<const impl> impl_' "$root/include/libpkgbuild/request.h" >/dev/null
grep -F 'std::shared_ptr<const impl> impl_' "$root/include/libpkgbuild/result.h" >/dev/null
grep -F 'libpkgbuild/build-input/1' "$root/src/request.cpp" >/dev/null
grep -F 'libpkgbuild/build-request/1' "$root/src/request.cpp" >/dev/null
grep -F 'libpkgbuild/build-result/1' "$root/src/result.cpp" >/dev/null
grep -F 'return declared.empty() ||' "$root/src/request.cpp" >/dev/null
grep -F 'hard-link metadata differs from its regular payload anchor' "$root/src/model.cpp" >/dev/null
