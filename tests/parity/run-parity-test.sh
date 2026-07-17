#!/bin/sh
set -eu

runner=$1
fakeroot=$2
root=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/pkgbuild-parity-runner.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
chmod 0755 "$work"

mkdir -p "$work/equal/case" "$work/different/case" "$work/identity/case"
printf '%s\n' 'name=fake' 'version=1.0' 'release=1' 'build() { :; }' \
    > "$work/equal/case/Pkgfile"
cp "$work/equal/case/Pkgfile" "$work/different/case/Pkgfile"
cp "$work/equal/case/Pkgfile" "$work/identity/case/Pkgfile"
: > "$work/different/case/candidate-different"
: > "$work/identity/case/candidate-name-different"

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
    | grep -q '^PASS case$'

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
printf '%s\n' "$output" | grep -q '^FAIL case$'
printf '%s\n' "$output" | grep -q 'payload-sha256'

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
printf '%s\n' "$identity_output" | grep -q '^FAIL case$'
printf '%s\n' "$identity_output" | grep -q 'package-filename'

echo 'parity corpus runner: PASS'
