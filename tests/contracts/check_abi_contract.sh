#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=${1:?source root required}
fail() { echo "abi-contract: $*" >&2; exit 1; }
manifest=$root/abi/libpkgbuild.exports
[ -s "$manifest" ] || fail 'reviewed ELF ABI manifest is absent'
[ "$(sed -n '/^_Z[A-Za-z0-9_]*$/p' "$manifest" | wc -l)" -eq 219 ] ||
    fail 'reviewed ELF ABI manifest must contain exactly 219 symbols'
[ "$(LC_ALL=C sort -u "$manifest" | wc -l)" -eq 219 ] ||
    fail 'reviewed ELF ABI manifest contains duplicate symbols'
! grep -E '^_ZNSt|^_ZN9__gnu_cxx' "$manifest" >/dev/null ||
    fail 'standard-library implementation symbol entered public ABI manifest'
demangled=$(mktemp)
trap 'rm -f "$demangled"' EXIT HUP INT TERM
c++filt < "$manifest" > "$demangled"
for class in \
    build_input build_input_set architecture_binding build_request build_result
 do
    ! grep -F "pkgbuild::$class::$class(std::shared_ptr" "$demangled" >/dev/null ||
        fail "private $class implementation constructor entered public ABI manifest"
done
for class in \
    build_policy payload_path payload_entry payload_manifest sealed_artifact \
    environment_policy
 do
    ! grep -F "pkgbuild::$class::$class(" "$demangled" >/dev/null ||
        fail "private $class constructor entered public ABI manifest"
done
for identity in \
    artifact_identity build_input_identity build_policy_identity \
    build_result_identity build_request_identity build_input_set_identity \
    artifact_binding_identity failure_evidence_identity \
    payload_manifest_identity environment_policy_identity \
    execution_evidence_identity
 do
    ! grep -F "pkgbuild::$identity::$identity(std::__cxx11::basic_string" \
        "$demangled" >/dev/null ||
        fail "private $identity constructor entered public ABI manifest"
done
grep -F '_ZN8pkgbuild13build_request4sealERKN10pkgresolve17resolution_resultENS1_26package_selection_identityENS_12build_policyE' "$manifest" >/dev/null ||
    fail 'build_request::seal is absent from reviewed ABI'
grep -F '_ZTIN8pkgbuild5errorE' "$manifest" >/dev/null ||
    fail 'public error RTTI is absent from reviewed ABI'
grep -F "soversion: '4'" "$root/src/meson.build" >/dev/null ||
    fail 'SONAME generation is not 4'
grep -F -- '--version-script=' "$root/src/meson.build" >/dev/null ||
    fail 'reviewed ELF export manifest is not linked'
