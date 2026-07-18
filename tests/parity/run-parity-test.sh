#!/bin/sh
set -eu

runner=$1
fakeroot=$2
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pkgbuild-parity-runner.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
chmod 0755 "$work"

make_case() {
    directory=$1
    name=$(basename "$directory")
    mkdir -p "$directory"
    printf '%s\n' \
        "name=$name" \
        'version=1.0' \
        'release=1' \
        'build() { :; }' > "$directory/Pkgfile"
}

mkdir -p "$work/equal" "$work/different" "$work/identity"
make_case "$work/equal/fake"
make_case "$work/different/fake"
make_case "$work/identity/fake"
: > "$work/different/fake/candidate-different"
: > "$work/identity/fake/candidate-name-different"

identity=
if [ "$(id -u)" -eq 0 ]; then
    identity="--build-user nobody"
fi

# Intentional splitting of the optional two-word identity argument.
# shellcheck disable=SC2086
"$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper /bin/true \
    --scanner /bin/true \
    --strip /bin/true \
    --work-dir "$work/run-equal" \
    $identity \
    "$work/equal" \
    | grep -q '^PASS fake$'

set +e
# shellcheck disable=SC2086
output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper /bin/true \
    --scanner /bin/true \
    --strip /bin/true \
    --work-dir "$work/run-different" \
    $identity \
    "$work/different")
status=$?
set -e

test "$status" -eq 1
printf '%s\n' "$output" | grep -q '^SEMANTIC_MISMATCH fake$'
printf '%s\n' "$output" | grep -q 'payload-sha256'
printf '%s\n' "$output" | grep -q '^FAILED_WORK '

set +e
# shellcheck disable=SC2086
identity_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper /bin/true \
    --scanner /bin/true \
    --strip /bin/true \
    --work-dir "$work/run-identity" \
    $identity \
    "$work/identity")
identity_status=$?
set -e

test "$identity_status" -eq 1
printf '%s\n' "$identity_output" | grep -q '^SEMANTIC_MISMATCH fake$'
printf '%s\n' "$identity_output" | grep -q 'package-filename'

# Exercise ordered manifests, baseline configuration, download forwarding,
# continuation after failures, and retained evidence.
manifest_root=$work/manifest
mkdir -p "$manifest_root/cases"
for name in pass legacy-fail candidate-fail different; do
    make_case "$manifest_root/cases/$name"
done
: > "$manifest_root/cases/pass/require-baseline"
: > "$manifest_root/cases/pass/require-download"
: > "$manifest_root/cases/legacy-fail/legacy-build-fail"
: > "$manifest_root/cases/candidate-fail/candidate-build-fail"
: > "$manifest_root/cases/different/candidate-different"
printf '%s\n' \
    '# ordered real-package manifest fixture' \
    'cases/pass' \
    'cases/legacy-fail' \
    'cases/candidate-fail' \
    'cases/different' > "$manifest_root/corpus.list"
printf '%s\n' 'PARITY_BASELINE_MARKER=yes' > "$manifest_root/pkgmk.conf"

set +e
# shellcheck disable=SC2086
manifest_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper /bin/true \
    --scanner /bin/true \
    --strip /bin/true \
    --config "$manifest_root/pkgmk.conf" \
    --download \
    --manifest "$manifest_root/corpus.list" \
    --work-dir "$work/run-manifest" \
    $identity)
manifest_status=$?
set -e

test "$manifest_status" -eq 1
printf '%s\n' "$manifest_output" | grep -q '^PASS pass$'
printf '%s\n' "$manifest_output" | grep -q '^LEGACY_BUILD_FAILED legacy-fail$'
printf '%s\n' "$manifest_output" | grep -q '^CANDIDATE_BUILD_FAILED candidate-fail$'
printf '%s\n' "$manifest_output" | grep -q '^SEMANTIC_MISMATCH different$'
printf '%s\n' "$manifest_output" | grep -q \
    '^SUMMARY pass=1 legacy-build-failed=1 candidate-build-failed=1 semantic-mismatch=1$'
failed_work=$(printf '%s\n' "$manifest_output" | sed -n 's/^FAILED_WORK //p')
test -n "$failed_work"
test ! -e "$failed_work/pass"
for name in legacy-fail candidate-fail different; do
    test -d "$failed_work/$name/pkgmk"
    test -d "$failed_work/$name/libpkgbuild"
    test -f "$failed_work/$name/comparison.txt"
done
grep -q '^status: LEGACY_BUILD_FAILED$' \
    "$failed_work/legacy-fail/comparison.txt"
grep -q '^status: CANDIDATE_BUILD_FAILED$' \
    "$failed_work/candidate-fail/comparison.txt"
grep -q '^status: SEMANTIC_MISMATCH$' \
    "$failed_work/different/comparison.txt"
test -f "$failed_work/legacy-fail/pkgmk/build.log"
test -f "$failed_work/candidate-fail/libpkgbuild/build.log"
grep -q 'legacy fixture stdout' "$failed_work/legacy-fail/pkgmk/build.log"
grep -q 'legacy fixture stderr' "$failed_work/legacy-fail/pkgmk/build.log"
grep -q 'candidate fixture stdout' \
    "$failed_work/candidate-fail/libpkgbuild/build.log"
grep -q 'candidate fixture stderr' \
    "$failed_work/candidate-fail/libpkgbuild/build.log"

# Duplicate package basenames are ambiguous in reports and retained trees.
mkdir -p "$work/duplicate/a" "$work/duplicate/b"
make_case "$work/duplicate/a/duplicate"
make_case "$work/duplicate/b/duplicate"
printf '%s\n' 'a/duplicate' 'b/duplicate' > "$work/duplicate/corpus.list"
set +e
# shellcheck disable=SC2086
duplicate_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper /bin/true \
    --scanner /bin/true \
    --strip /bin/true \
    --manifest "$work/duplicate/corpus.list" \
    --work-dir "$work/run-duplicate" \
    $identity 2>&1)
duplicate_status=$?
set -e

test "$duplicate_status" -eq 2
printf '%s\n' "$duplicate_output" | grep -q 'duplicate package basename'

set +e
# shellcheck disable=SC2086
missing_config_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper /bin/true \
    --scanner /bin/true \
    --strip /bin/true \
    --config "$work/does-not-exist.conf" \
    --work-dir "$work/run-missing-config" \
    $identity \
    "$work/equal" 2>&1)
missing_config_status=$?
set -e

test "$missing_config_status" -eq 2
printf '%s\n' "$missing_config_output" | grep -q \
    'baseline configuration is not a regular file'

echo 'parity corpus runner: PASS'
