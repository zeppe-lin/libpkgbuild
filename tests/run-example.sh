#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$root/.example-build
local_fixture=$root/tests/fixtures/hello
archive_fixture=$root/tests/fixtures/hello-archive

rm -rf "$build" "$local_fixture/work" "$archive_fixture/work"
rm -f "$local_fixture"/*.pkg.tar.* "$archive_fixture"/*.pkg.tar.*
mkdir -p "$build" "$build/upstream/payload"
printf '%s\n' 'hello through curl and libarchive' \
	> "$build/upstream/payload/message.txt"
tar -C "$build/upstream" -czf "$archive_fixture/upstream.tar.gz" payload

cxx=${CXX:-c++}
common_cxxflags="-std=c++17 -Wall -Wextra -Wpedantic"

# Intentional word splitting for compiler and pkg-config arguments.
# shellcheck disable=SC2086,SC2046
"$cxx" $common_cxxflags -fPIC -shared \
	-I"$root/include" -I"$root/libpkgbuild" \
	-Wl,-soname,libpkgbuild.so.0 \
	"$root/libpkgbuild/types.cpp" \
	"$root/libpkgbuild/process.cpp" \
	"$root/libpkgbuild/engine.cpp" \
	"$root/libpkgbuild/backends/pkgfile.cpp" \
	"$root/libpkgbuild/backends/curl.cpp" \
	"$root/libpkgbuild/backends/libarchive.cpp" \
	$(pkg-config --cflags --libs libarchive libcurl) \
	-o "$build/libpkgbuild.so.0.1.0"
ln -s libpkgbuild.so.0.1.0 "$build/libpkgbuild.so.0"
ln -s libpkgbuild.so.0 "$build/libpkgbuild.so"

# shellcheck disable=SC2086
"$cxx" $common_cxxflags \
	-I"$root/include" \
	-DPKGBUILD_PKGFILE_HELPER=\"$root/libpkgbuild/pkgbuild-pkgfile.in\" \
	"$root/tools/pkgbuild-example.cpp" \
	-L"$build" -Wl,-rpath,"$build" -lpkgbuild \
	-o "$build/pkgbuild-example"

"$build/pkgbuild-example" --helper "$root/libpkgbuild/pkgbuild-pkgfile.in" \
	--work-dir "$build/local-work" --package-dir "$build/packages" \
	"$local_fixture"

test -f "$build/packages/hello#1.0-1.pkg.tar.gz"
tar -tzf "$build/packages/hello#1.0-1.pkg.tar.gz" \
	| grep -qx 'usr/share/hello/message.txt'

"$build/pkgbuild-example" --download \
	--helper "$root/libpkgbuild/pkgbuild-pkgfile.in" \
	--source-dir "$build/sources" \
	--work-dir "$build/archive-work" \
	--package-dir "$build/packages" \
	"$archive_fixture"

test -f "$build/packages/hello-archive#1.0-1.pkg.tar.gz"
tar -xOzf "$build/packages/hello-archive#1.0-1.pkg.tar.gz" \
	usr/share/hello-archive/message.txt \
	| grep -qx 'hello through curl and libarchive'

echo "libpkgbuild example: PASS"
