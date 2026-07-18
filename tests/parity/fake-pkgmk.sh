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
    echo "legacy fixture failure"
    exit 8
fi

tree=$PKGMK_WORK_DIR/fake-root
rm -rf "$tree"
mkdir -p "$tree/usr/share/parity"
printf '%s\n' 'same payload' > "$tree/usr/share/parity/value"
tar -C "$tree" -czf \
    "$PKGMK_PACKAGE_DIR/$name#$version-$release.pkg.tar.gz" usr
