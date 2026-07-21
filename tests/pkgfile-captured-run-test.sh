#!/bin/sh
set -eu

helper=$1
root=$(mktemp -d "${TMPDIR:-/tmp}/pkgbuild-captured-run.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM

origin=$root/original/accepted
recipe=$root/materialized/accepted
source_dir=$root/sources
package_dir=$root/packages
work_dir=$root/work/.pkgbuild.test
src=$work_dir/src
pkg=$work_dir/pkg
mkdir -p "$origin" "$source_dir" "$package_dir" "$src" "$pkg"

cat > "$origin/Pkgfile" <<'PKGFILE'
name=poison-name
version=poison-version
release=poison-release
PKGMK_ROOT=/poison/root
PKGMK_SOURCE_DIR=/poison/sources
PKGMK_PACKAGE_DIR=/poison/packages
PKGMK_WORK_DIR=/poison/work
PKGMK_ARCHIVE_FORMAT=v7
PKGMK_COMPRESSION_MODE=gz
PKGMK_ARCH=poison-arch
SRC=/poison/src
PKG=/poison/pkg
build() {
    {
        printf '%s\n' \
            "$PKGMK_ROOT" \
            "$PKGMK_SOURCE_DIR" \
            "$PKGMK_PACKAGE_DIR" \
            "$PKGMK_WORK_DIR" \
            "$PKGMK_ARCHIVE_FORMAT" \
            "$PKGMK_COMPRESSION_MODE" \
            "$PKGMK_ARCH" \
            "$SRC" \
            "$PKG" \
            "$name" \
            "$version" \
            "$release"
        pwd
        cat "$PKGMK_ROOT/sibling.txt"
    } > "$PKG/observed"
}
PKGFILE
printf '%s\n' 'captured corpse' > "$origin/sibling.txt"
: > "$origin/.32bit"
printf '%s\n' 'exit 97' > "$origin/pkgmk.conf"

mkdir -p "$(dirname "$recipe")"
cp -R "$origin" "$recipe"
rm -rf "$root/original"

"$helper" run-captured \
    "$recipe" \
    "$source_dir" \
    "$package_dir" \
    "$work_dir" \
    pax \
    xz \
    native \
    "$src" \
    "$pkg" \
    accepted \
    1.2 \
    3

cat > "$root/expected" <<EOF_EXPECTED
$recipe
$source_dir
$package_dir
$work_dir
pax
xz
64
$src
$pkg
accepted
1.2
3
$src
captured corpse
EOF_EXPECTED

cmp "$root/expected" "$pkg/observed"
echo 'captured Pkgfile execution: PASS'
