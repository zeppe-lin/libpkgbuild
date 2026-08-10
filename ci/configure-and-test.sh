#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

[ "$#" -ge 2 ] || {
  echo 'usage: configure-and-test.sh BUILD-DIR {shared|static} [MESON-ARG...]' >&2
  exit 2
}
build_dir=$1
link_mode=$2
shift 2
case $link_mode in shared|static) ;; *) exit 2 ;; esac
root=$(CDPATH= cd "$(dirname "$0")/.." && pwd)
case $build_dir in /*) build=$build_dir ;; *) build=$(pwd)/$build_dir ;; esac
dependency_prefix=$build/dependencies
rm -rf "$build"
mkdir -p "$build"

setup_dependency()
{
  source_dir=$1
  output_dir=$2
  shift 2
  meson setup "$output_dir" "$source_dir" \
    --wrap-mode=nofallback \
    --fatal-meson-warnings \
    --prefix="$dependency_prefix" \
    --libdir=lib \
    --buildtype="${MESON_BUILDTYPE:-debug}" \
    -Ddefault_library="$link_mode" \
    -Dlink_mode="$link_mode" \
    -Dtests=disabled \
    -Dman_pages=disabled \
    -Dwerror=true \
    ${MESON_SANITIZE:+-Db_sanitize="$MESON_SANITIZE"} \
    ${MESON_SANITIZE:+-Db_lundef=false} \
    "$@"
  meson compile -C "$output_dir"
  meson install -C "$output_dir"
}

for variable in \
    LIBPKGSOURCE_SOURCE LIBPKGCATALOG_SOURCE LIBPKGSTATE_SOURCE \
    LIBPKGRESOLVE_SOURCE
do
  eval "value=\${$variable:-}"
  [ -n "$value" ] || { echo "set $variable" >&2; exit 2; }
done

setup_dependency "$LIBPKGSOURCE_SOURCE" "$build/libpkgsource"
export PKG_CONFIG_PATH="$dependency_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$dependency_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
setup_dependency "$LIBPKGSTATE_SOURCE" "$build/libpkgstate"
setup_dependency "$LIBPKGCATALOG_SOURCE" "$build/libpkgcatalog"
setup_dependency "$LIBPKGRESOLVE_SOURCE" "$build/libpkgresolve"

meson setup "$build/product" "$root" \
  --wrap-mode=nofallback \
  --fatal-meson-warnings \
  --prefix="$build/install" \
  --libdir=lib \
  --buildtype="${MESON_BUILDTYPE:-debug}" \
  -Ddefault_library="$link_mode" \
  -Dlink_mode="$link_mode" \
  -Dtests=enabled \
  -Dman_pages=enabled \
  -Dwerror=true \
  ${MESON_SANITIZE:+-Db_sanitize="$MESON_SANITIZE"} \
  ${MESON_SANITIZE:+-Db_lundef=false} \
  "$@"
meson compile -C "$build/product"
meson test -C "$build/product" --no-rebuild --print-errorlogs
meson install -C "$build/product"

consumer="$root/tests/installed/consumer.cpp"
consumer_bin="$build/installed-consumer"
consumer_pkgconfig="$build/install/lib/pkgconfig:$dependency_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
consumer_flags=$(PKG_CONFIG_PATH="$consumer_pkgconfig" pkg-config --cflags libpkgbuild)
if [ "$link_mode" = static ]; then
  consumer_libs=$(PKG_CONFIG_PATH="$consumer_pkgconfig" pkg-config --static --libs libpkgbuild)
else
  consumer_libs=$(PKG_CONFIG_PATH="$consumer_pkgconfig" pkg-config --libs libpkgbuild)
fi
consumer_sanitize=
if [ -n "${MESON_SANITIZE:-}" ]; then
  consumer_sanitize="-fsanitize=$MESON_SANITIZE"
fi
# shellcheck disable=SC2086
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  $consumer_sanitize $consumer_flags "$consumer" -o "$consumer_bin" \
  $consumer_libs
LD_LIBRARY_PATH="$build/install/lib:$dependency_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$consumer_bin"
