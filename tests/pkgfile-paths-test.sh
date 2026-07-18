#!/bin/sh
set -eu

helper=$1
root=$(mktemp -d "${TMPDIR:-/tmp}/pkgbuild-pkgfile-paths.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM

recipe=$root/path-test
source_dir=$root/sources
package_dir=$root/packages
work_dir=$root/work/.pkgbuild.test
src=$work_dir/src
pkg=$work_dir/pkg
mkdir -p "$recipe" "$source_dir" "$package_dir" "$src" "$pkg"

cat > "$recipe/Pkgfile" <<'PKGFILE'
name=path-test
version=1.0
release=1
build() {
    printf '%s\n' \
        "$PKGMK_ROOT" \
        "$PKGMK_SOURCE_DIR" \
        "$PKGMK_PACKAGE_DIR" \
        "$PKGMK_WORK_DIR" \
        "$PKGMK_ARCHIVE_FORMAT" \
        "$PKGMK_COMPRESSION_MODE" \
        "$SRC" \
        "$PKG" > "$PKG/paths"
}
PKGFILE

cat > "$root/pkgmk.conf" <<EOF_CONFIG
PKGMK_ROOT=$root/wrong-root
PKGMK_SOURCE_DIR=$root/wrong-sources
PKGMK_PACKAGE_DIR=$root/wrong-packages
PKGMK_WORK_DIR=$root/wrong-work
PKGMK_ARCHIVE_FORMAT=v7
PKGMK_COMPRESSION_MODE=xz
SRC=$root/wrong-src
PKG=$root/wrong-pkg
EOF_CONFIG

"$helper" run \
    "$recipe" \
    "$root/pkgmk.conf" \
    "$source_dir" \
    "$package_dir" \
    "$work_dir" \
    gnutar \
    gz \
    "$src" \
    "$pkg" \
    path-test \
    1.0 \
    1

cat > "$root/expected" <<EOF_EXPECTED
$recipe
$source_dir
$package_dir
$work_dir
gnutar
gz
$src
$pkg
EOF_EXPECTED

cmp "$root/expected" "$pkg/paths"
echo 'pkgfile execution paths: PASS'
