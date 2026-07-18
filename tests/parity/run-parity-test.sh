#!/bin/sh
set -eu

runner=$1
fakeroot=$2
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
helper=$root/../../libpkgbuild/pkgbuild-pkgfile.in
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
    --helper "$helper" \
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
    --helper "$helper" \
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
    --helper "$helper" \
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
for name in pass sibling legacy-fail candidate-fail different \
    candidate-unstable legacy-unstable; do
    make_case "$manifest_root/cases/$name"
done
: > "$manifest_root/cases/pass/require-baseline"
: > "$manifest_root/cases/pass/require-download"
mkdir -p "$manifest_root/cases/shared"
printf '%s
' 'shared source payload' > \
    "$manifest_root/cases/shared/payload.txt"
printf '%s
' \
    'name=sibling' \
    'version=1.0' \
    'release=1' \
    'source="../shared/payload.txt"' \
    'build() { :; }' > "$manifest_root/cases/sibling/Pkgfile"
printf '%s  %s
' \
    "$(md5sum "$manifest_root/cases/shared/payload.txt" | cut -d' ' -f1)" \
    'payload.txt' > "$manifest_root/cases/sibling/.md5sum"
: > "$manifest_root/cases/sibling/require-sibling-source"
: > "$manifest_root/cases/legacy-fail/legacy-build-fail"
: > "$manifest_root/cases/candidate-fail/candidate-build-fail"
: > "$manifest_root/cases/different/candidate-different"
: > "$manifest_root/cases/candidate-unstable/candidate-unstable"
: > "$manifest_root/cases/legacy-unstable/legacy-unstable"
printf '%s\n' \
    '# ordered real-package manifest fixture' \
    'cases/pass' \
    'cases/sibling' \
    'cases/legacy-fail' \
    'cases/candidate-fail' \
    'cases/different' \
    'cases/candidate-unstable' \
    'cases/legacy-unstable' > "$manifest_root/corpus.list"
printf '%s\n' 'PARITY_BASELINE_MARKER=yes' > "$manifest_root/pkgmk.conf"

set +e
# shellcheck disable=SC2086
manifest_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper "$helper" \
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
printf '%s\n' "$manifest_output" | grep -q '^PASS sibling$'
printf '%s\n' "$manifest_output" | grep -q '^LEGACY_BUILD_FAILED legacy-fail$'
printf '%s\n' "$manifest_output" | grep -q '^CANDIDATE_BUILD_FAILED candidate-fail$'
printf '%s\n' "$manifest_output" | grep -q '^SEMANTIC_MISMATCH different$'
printf '%s\n' "$manifest_output" | grep -q \
    '^NONDETERMINISTIC_OUTPUT candidate-unstable$'
printf '%s\n' "$manifest_output" | grep -q \
    '^NONDETERMINISTIC_OUTPUT legacy-unstable$'
printf '%s\n' "$manifest_output" | grep -q \
    '^SUMMARY pass=2 legacy-build-failed=1 candidate-build-failed=1 nondeterministic-output=2 semantic-mismatch=1$'
failed_work=$(printf '%s\n' "$manifest_output" | sed -n 's/^FAILED_WORK //p')
test -n "$failed_work"
test ! -e "$failed_work/pass"
test ! -e "$failed_work/sibling"
for name in legacy-fail candidate-fail different \
    candidate-unstable legacy-unstable; do
    test -f "$failed_work/$name/comparison.txt"
done
test -d "$failed_work/legacy-fail/pkgmk/run-1"
test -d "$failed_work/legacy-fail/libpkgbuild/run-1"
test -d "$failed_work/candidate-fail/libpkgbuild/run-1"
test ! -e "$failed_work/candidate-fail/pkgmk"
for name in different candidate-unstable legacy-unstable; do
    test -d "$failed_work/$name/pkgmk/run-1"
    test -d "$failed_work/$name/libpkgbuild/run-1"
done
grep -q '^status: LEGACY_BUILD_FAILED$' \
    "$failed_work/legacy-fail/comparison.txt"
grep -q '^status: CANDIDATE_BUILD_FAILED$' \
    "$failed_work/candidate-fail/comparison.txt"
grep -q '^status: SEMANTIC_MISMATCH$' \
    "$failed_work/different/comparison.txt"
for name in candidate-unstable legacy-unstable; do
    grep -q '^status: NONDETERMINISTIC_OUTPUT$' \
        "$failed_work/$name/comparison.txt"
done
grep -q '^detail: engine: libpkgbuild$' \
    "$failed_work/candidate-unstable/comparison.txt"
grep -q '^detail: engine: pkgmk$' \
    "$failed_work/legacy-unstable/comparison.txt"
grep -q '^detail: repeat-mismatch: ' \
    "$failed_work/candidate-unstable/comparison.txt"
grep -q '^detail: repeat-mismatch: ' \
    "$failed_work/legacy-unstable/comparison.txt"

test -f "$failed_work/legacy-fail/pkgmk/run-1/build.log"
test -f "$failed_work/candidate-fail/libpkgbuild/run-1/build.log"
grep -q 'legacy fixture stdout' \
    "$failed_work/legacy-fail/pkgmk/run-1/build.log"
grep -q 'legacy fixture stderr' \
    "$failed_work/legacy-fail/pkgmk/run-1/build.log"
grep -q 'candidate fixture stdout' \
    "$failed_work/candidate-fail/libpkgbuild/run-1/build.log"
grep -q 'candidate fixture stderr' \
    "$failed_work/candidate-fail/libpkgbuild/run-1/build.log"

for engine in pkgmk libpkgbuild; do
    for run in run-1 run-2; do
        test -f "$failed_work/different/$engine/$run/build.log"
        test -f "$failed_work/different/$engine/$run/workspace.txt"
        test -d "$failed_work/different/$engine/$run/packages"
    done
done
cmp "$failed_work/different/libpkgbuild/run-1/workspace.txt" \
    "$failed_work/different/libpkgbuild/run-2/workspace.txt"
cmp "$failed_work/different/libpkgbuild/run-1/workspace.txt" \
    "$failed_work/different/pkgmk/run-1/workspace.txt"
cmp "$failed_work/different/libpkgbuild/run-1/workspace.txt" \
    "$failed_work/different/pkgmk/run-2/workspace.txt"
test -f "$failed_work/different/cross-comparison.txt"
test -f "$failed_work/different/libpkgbuild/repeat-comparison.txt"
test -f "$failed_work/different/pkgmk/repeat-comparison.txt"
grep -q '^equivalent$' \
    "$failed_work/different/libpkgbuild/repeat-comparison.txt"
grep -q '^equivalent$' \
    "$failed_work/different/pkgmk/repeat-comparison.txt"

test -f "$failed_work/candidate-unstable/libpkgbuild/run-2/build.log"
test -f \
    "$failed_work/candidate-unstable/libpkgbuild/repeat-comparison.txt"
test ! -e "$failed_work/candidate-unstable/pkgmk/run-2"
test ! -e "$failed_work/candidate-unstable/pkgmk/repeat-comparison.txt"
test -f "$failed_work/legacy-unstable/libpkgbuild/run-2/build.log"
test -f "$failed_work/legacy-unstable/pkgmk/run-2/build.log"
test -f \
    "$failed_work/legacy-unstable/libpkgbuild/repeat-comparison.txt"
test -f "$failed_work/legacy-unstable/pkgmk/repeat-comparison.txt"
grep -q '^equivalent$' \
    "$failed_work/legacy-unstable/libpkgbuild/repeat-comparison.txt"

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
    --helper "$helper" \
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
    --helper "$helper" \
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
