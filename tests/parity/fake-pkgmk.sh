#!/bin/sh
set -eu

if [ "$(basename "$PWD")" != fake ]; then
    echo "fake-pkgmk: recipe basename is not package name" >&2
    exit 2
fi

config=
while [ "$#" -gt 0 ]; do
    case $1 in
    -cf)
        config=$2
        shift 2
        ;;
    *)
        shift
        ;;
    esac
done

. "$config"
tree=$PKGMK_WORK_DIR/fake-root
rm -rf "$tree"
mkdir -p "$tree/usr/share/parity"
printf '%s\n' 'same payload' > "$tree/usr/share/parity/value"
tar -C "$tree" -czf "$PKGMK_PACKAGE_DIR/fake#1.0-1.pkg.tar.gz" usr
