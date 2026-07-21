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
equal_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper "$helper" \
    --scanner /bin/true \
    --strip /bin/true \
    --work-dir "$work/run-equal" \
    $identity \
    "$work/equal")
printf '%s\n' "$equal_output" | grep -q '^\[1/1\] RUN fake$'
printf '%s\n' "$equal_output" | grep -q '^\[1/1\] PASS fake$'
equal_report=$(printf '%s\n' "$equal_output" | sed -n 's/^REPORT //p')
equal_results=$(printf '%s\n' "$equal_output" | sed -n 's/^RESULTS //p')
test -f "$equal_report"
test -f "$equal_results"
grep -q '^pass: 1$' "$equal_report"
grep -q '^none$' "$equal_report"
awk -F '\t' 'NR == 2 && $1 == 1 && $2 == "fake" && $3 == "PASS" { found=1 } END { exit !found }' "$equal_results"

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
printf '%s\n' "$output" | grep -q '^\[1/1\] SEMANTIC_MISMATCH fake$'
different_report=$(printf '%s\n' "$output" | sed -n 's/^REPORT //p')
grep -q 'payload-sha256' "$different_report"
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
printf '%s\n' "$identity_output" | grep -q '^\[1/1\] SEMANTIC_MISMATCH fake$'
identity_report=$(printf '%s\n' "$identity_output" | sed -n 's/^REPORT //p')
grep -q 'package-filename' "$identity_report"

# Exercise ordered manifests, legacy baseline configuration, download forwarding,
# continuation after failures, and retained evidence.
manifest_root=$work/manifest
mkdir -p "$manifest_root/cases"
for name in pass sibling glob preparation-fail legacy-fail candidate-fail \
    artifact-fail different many-details candidate-unstable legacy-unstable; do
    make_case "$manifest_root/cases/$name"
done
: > "$manifest_root/cases/pass/require-baseline"
: > "$manifest_root/cases/pass/require-download"
mkdir -p "$manifest_root/cases/shared"
printf '%s\n' 'shared source payload' > \
    "$manifest_root/cases/shared/payload.txt"
printf '%s\n' \
    'name=sibling' \
    'version=1.0' \
    'release=1' \
    'source="../shared/payload.txt"' \
    'build() { :; }' > "$manifest_root/cases/sibling/Pkgfile"
printf '%s  %s\n' \
    "$(md5sum "$manifest_root/cases/shared/payload.txt" | cut -d' ' -f1)" \
    'payload.txt' > "$manifest_root/cases/sibling/.md5sum"
: > "$manifest_root/cases/sibling/require-sibling-source"
printf '%s\n' 'alpha' > "$manifest_root/cases/shared/alpha.patch"
printf '%s\n' 'beta' > "$manifest_root/cases/shared/beta.patch"
printf '%s\n' \
    'name=glob' \
    'version=1.0' \
    'release=1' \
    'source="../shared/*.patch"' \
    'build() { :; }' > "$manifest_root/cases/glob/Pkgfile"
printf '%s  %s\n' \
    "$(md5sum "$manifest_root/cases/shared/alpha.patch" | cut -d' ' -f1)" \
    'alpha.patch' \
    "$(md5sum "$manifest_root/cases/shared/beta.patch" | cut -d' ' -f1)" \
    'beta.patch' > "$manifest_root/cases/glob/.md5sum"
: > "$manifest_root/cases/glob/require-glob-sources"
printf '%s\n' \
    'name=preparation-fail' \
    'version=1.0' \
    'release=1' \
    'source="../missing/*.patch"' \
    'build() { :; }' > "$manifest_root/cases/preparation-fail/Pkgfile"
: > "$manifest_root/cases/legacy-fail/legacy-build-fail"
: > "$manifest_root/cases/candidate-fail/candidate-build-fail"
: > "$manifest_root/cases/artifact-fail/candidate-artifact-fail"
: > "$manifest_root/cases/different/candidate-different"
: > "$manifest_root/cases/many-details/candidate-many-differences"
: > "$manifest_root/cases/candidate-unstable/candidate-unstable"
: > "$manifest_root/cases/legacy-unstable/legacy-unstable"
printf '%s\n' \
    '# ordered real-package manifest fixture' \
    'cases/pass' \
    'cases/sibling' \
    'cases/glob' \
    'cases/preparation-fail' \
    'cases/legacy-fail' \
    'cases/candidate-fail' \
    'cases/artifact-fail' \
    'cases/different' \
    'cases/many-details' \
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
    --report "$work/reports/manifest.txt" \
    --report-tail 2 \
    --report-details 2 \
    $identity)
manifest_status=$?
set -e

test "$manifest_status" -eq 1
printf '%s\n' "$manifest_output" | grep -q '^\[1/11\] PASS pass$'
printf '%s\n' "$manifest_output" | grep -q '^\[2/11\] PASS sibling$'
printf '%s\n' "$manifest_output" | grep -q '^\[3/11\] PASS glob$'
printf '%s\n' "$manifest_output" | grep -q \
    '^\[4/11\] CASE_PREPARATION_FAILED preparation-fail$'
printf '%s\n' "$manifest_output" | grep -q '^\[5/11\] LEGACY_BUILD_FAILED legacy-fail$'
printf '%s\n' "$manifest_output" | grep -q '^\[6/11\] CANDIDATE_BUILD_FAILED candidate-fail$'
printf '%s\n' "$manifest_output" | grep -q '^\[7/11\] ARTIFACT_INSPECTION_FAILED artifact-fail$'
printf '%s\n' "$manifest_output" | grep -q '^\[8/11\] SEMANTIC_MISMATCH different$'
printf '%s\n' "$manifest_output" | grep -q '^\[9/11\] SEMANTIC_MISMATCH many-details$'
printf '%s\n' "$manifest_output" | grep -q \
    '^\[10/11\] NONDETERMINISTIC_OUTPUT candidate-unstable$'
printf '%s\n' "$manifest_output" | grep -q \
    '^\[11/11\] NONDETERMINISTIC_OUTPUT legacy-unstable$'
printf '%s\n' "$manifest_output" | grep -q \
    '^SUMMARY pass=3 case-preparation-failed=1 legacy-build-failed=1 candidate-build-failed=1 artifact-inspection-failed=1 nondeterministic-output=2 semantic-mismatch=2$'
! printf '%s\n' "$manifest_output" | grep -q 'candidate fixture stdout'
report=$(printf '%s\n' "$manifest_output" | sed -n 's/^REPORT //p')
campaign_report=$(printf '%s\n' "$manifest_output" | sed -n 's/^CAMPAIGN_REPORT //p')
results=$(printf '%s\n' "$manifest_output" | sed -n 's/^RESULTS //p')
test "$report" = "$work/reports/manifest.txt"
test -f "$report"
test -f "$campaign_report"
test -f "$results"
cmp "$report" "$campaign_report"
failed_work=$(printf '%s\n' "$manifest_output" | sed -n 's/^FAILED_WORK //p')
test -n "$failed_work"
test ! -e "$failed_work/pass"
test ! -e "$failed_work/sibling"
test ! -e "$failed_work/glob"
for name in preparation-fail legacy-fail candidate-fail artifact-fail \
    different many-details candidate-unstable legacy-unstable; do
    test -f "$failed_work/$name/comparison.txt"
done
test ! -e "$failed_work/preparation-fail/pkgmk"
test ! -e "$failed_work/preparation-fail/libpkgbuild"
grep -q '^status: CASE_PREPARATION_FAILED$' \
    "$failed_work/preparation-fail/comparison.txt"
grep -q '^detail: setup-error: local source is not a regular file: ../missing/\*.patch$' \
    "$failed_work/preparation-fail/comparison.txt"
test -d "$failed_work/legacy-fail/pkgmk/run-1"
test -d "$failed_work/legacy-fail/libpkgbuild/run-1"
test -d "$failed_work/candidate-fail/libpkgbuild/run-1"
test ! -e "$failed_work/candidate-fail/pkgmk"
test -d "$failed_work/artifact-fail/libpkgbuild/run-1"
test ! -e "$failed_work/artifact-fail/pkgmk"
for name in different many-details candidate-unstable legacy-unstable; do
    test -d "$failed_work/$name/pkgmk/run-1"
    test -d "$failed_work/$name/libpkgbuild/run-1"
done
grep -q '^status: LEGACY_BUILD_FAILED$' \
    "$failed_work/legacy-fail/comparison.txt"
grep -q '^status: CANDIDATE_BUILD_FAILED$' \
    "$failed_work/candidate-fail/comparison.txt"
grep -q '^status: ARTIFACT_INSPECTION_FAILED$' \
    "$failed_work/artifact-fail/comparison.txt"
grep -q '^detail: engine: libpkgbuild$' \
    "$failed_work/artifact-fail/comparison.txt"
grep -q '^detail: artifact-error: thin ar archive is unsupported:' \
    "$failed_work/artifact-fail/comparison.txt"
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

grep -q '^cases: 11$' "$report"
grep -q '^pass: 3$' "$report"
grep -q '^semantic-mismatch: 2$' "$report"
grep -q '^\[candidate-fail\]$' "$report"
grep -q '^last 2 lines:$' "$report"
grep -q '^  candidate fixture stdout$' "$report"
grep -q '^  candidate fixture stderr$' "$report"
! grep -q '^  candidate fixture line 3$' "$report"
grep -q '^\[many-details\]$' "$report"
grep -q '^shown: 2$' "$report"
grep -Eq '^  [1-9][0-9]* additional details omitted$' "$report"
awk -F '\t' 'NR == 1 && $1 == "index" && $2 == "name" && $3 == "status" && $4 == "source" && $5 == "elapsed_seconds" && $6 == "evidence" { found=1 } END { exit !found }' "$results"
awk -F '\t' '$1 == 1 && $2 == "pass" && $3 == "PASS" { found=1 } END { exit !found }' "$results"
awk -F '\t' '$2 == "candidate-fail" && $3 == "CANDIDATE_BUILD_FAILED" && $6 == "failed/candidate-fail" { found=1 } END { exit !found }' "$results"
test "$(wc -l < "$results")" -eq 12

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

printf '%s\n' 'cases/candidate-fail' > "$manifest_root/verbose.list"
set +e
# shellcheck disable=SC2086
verbose_output=$("$runner" \
    --pkgmk "$root/fake-pkgmk.sh" \
    --pkgbuild "$root/fake-pkgbuild.sh" \
    --fakeroot "$fakeroot" \
    --helper "$helper" \
    --scanner /bin/true \
    --strip /bin/true \
    --verbose-builds \
    --manifest "$manifest_root/verbose.list" \
    --work-dir "$work/run-verbose" \
    $identity)
verbose_status=$?
set -e
test "$verbose_status" -eq 1
printf '%s\n' "$verbose_output" | grep -q 'candidate fixture stdout'
printf '%s\n' "$verbose_output" | grep -q 'candidate fixture stderr'

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
