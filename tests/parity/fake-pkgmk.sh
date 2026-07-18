#!/bin/sh
set -eu

package_name=$(basename "$PWD")
# Fixture Pkgfiles contain data assignments and an inert build function.
# shellcheck disable=SC1091
. ./Pkgfile

if [ "$package_name" != "$name" ]; then
    echo "fake-pkgmk: recipe basename is not package name" >&2
    exit 2
fi

config=
download=no
while [ "$#" -gt 0 ]; do
    case $1 in
    -cf)
        config=$2
        shift 2
        ;;
    -d)
        download=yes
        shift
        ;;
    *)
        shift
        ;;
    esac
done

# shellcheck disable=SC1090
. "$config"
if [ -f require-baseline ] && [ "${PARITY_BASELINE_MARKER:-}" != yes ]; then
    echo "fake-pkgmk: baseline configuration was not sourced" >&2
    exit 2
fi
if [ -f require-download ] && [ "$download" != yes ]; then
    echo "fake-pkgmk: download mode was not forwarded" >&2
    exit 2
fi
if [ -f legacy-build-fail ]; then
    echo "legacy fixture stdout"
    echo "legacy fixture stderr" >&2
    exit 8
fi

# Match pkgmk startup: its work directory is removed before the build.
rm -rf "$PKGMK_WORK_DIR"
mkdir -p "$PKGMK_WORK_DIR"
if [ ! -d "$TMPDIR" ]; then
    echo "fake-pkgmk: TMPDIR disappeared with PKGMK_WORK_DIR" >&2
    exit 8
fi

tree=$PKGMK_WORK_DIR/fake-root
mkdir -p "$tree/usr/share/parity"
printf '%s\n' 'same payload' > "$tree/usr/share/parity/value"
printf '%s\n' "$PKGMK_WORK_DIR" > "$tree/usr/share/parity/workspace"
printf '%s\n' "$TMPDIR" > "$tree/usr/share/parity/tmpdir"
tar -C "$tree" -czf \
    "$PKGMK_PACKAGE_DIR/$name#$version-$release.pkg.tar.gz" usr
