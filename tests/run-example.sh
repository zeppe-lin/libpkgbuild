#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
build=$root/.example-build
local_fixture=$root/tests/fixtures/hello
archive_fixture=$root/tests/fixtures/hello-archive
archive_md5=$archive_fixture/.md5sum

rm -rf "$build" "$local_fixture/work" "$archive_fixture/work"
rm -f "$local_fixture"/*.pkg.tar.* "$archive_fixture"/*.pkg.tar.* "$archive_md5"
trap 'rm -f "$archive_md5"' EXIT HUP INT TERM
mkdir -p "$build" "$build/upstream/payload"
printf '%s\n' 'hello through curl and libarchive' \
	> "$build/upstream/payload/message.txt"
tar -C "$build/upstream" -czf "$archive_fixture/upstream.tar.gz" payload
md5sum "$archive_fixture/upstream.tar.gz" \
	| sed 's|  .*|  payload.tar.gz|' > "$archive_md5"

cxx=${CXX:-c++}
common_cxxflags="-std=c++17 -Wall -Wextra -Wpedantic"
build_user_args=
if [ "$(id -u)" -eq 0 ]; then
	build_user_args="--build-user nobody"
fi

# Intentional word splitting for compiler and pkg-config arguments.
# shellcheck disable=SC2086,SC2046
"$cxx" $common_cxxflags -fPIC -shared \
	-I"$root/include" -I"$root/libpkgbuild" \
	-Wl,-soname,libpkgbuild.so.0 \
	"$root/libpkgbuild/types.cpp" \
	"$root/libpkgbuild/source.cpp" \
	"$root/libpkgbuild/process.cpp" \
	"$root/libpkgbuild/stage.cpp" \
	"$root/libpkgbuild/engine.cpp" \
	"$root/libpkgbuild/backends/pkgfile.cpp" \
	"$root/libpkgbuild/backends/curl.cpp" \
	"$root/libpkgbuild/backends/libarchive.cpp" \
	$(pkg-config --cflags --libs libarchive libcurl) \
	-o "$build/libpkgbuild.so.0.3.0"
ln -s libpkgbuild.so.0.3.0 "$build/libpkgbuild.so.0"
ln -s libpkgbuild.so.0 "$build/libpkgbuild.so"

# shellcheck disable=SC2086
"$cxx" $common_cxxflags \
	-I"$root/include" -I"$root/libpkgbuild" \
	"$root/tools/pkgbuild-stage-scan.cpp" \
	-L"$build" -Wl,-rpath,"$build" -lpkgbuild \
	-o "$build/pkgbuild-stage-scan"

# shellcheck disable=SC2086
"$cxx" $common_cxxflags \
	-I"$root/include" \
	-DPKGBUILD_PKGFILE_HELPER=\"$root/libpkgbuild/pkgbuild-pkgfile.in\" \
	-DPKGBUILD_STAGE_SCANNER=\"$build/pkgbuild-stage-scan\" \
	"$root/tools/pkgbuild-example.cpp" \
	-L"$build" -Wl,-rpath,"$build" -lpkgbuild \
	-o "$build/pkgbuild-example"

# Intentional word splitting for the optional build-user arguments.
# shellcheck disable=SC2086
"$build/pkgbuild-example" $build_user_args --helper "$root/libpkgbuild/pkgbuild-pkgfile.in" \
	--scanner "$build/pkgbuild-stage-scan" --fakeroot /usr/bin/fakeroot \
	--work-dir "$build/local-work" --package-dir "$build/packages" \
	"$local_fixture"

test -f "$build/packages/hello#1.0-1.pkg.tar.gz"
tar -tzf "$build/packages/hello#1.0-1.pkg.tar.gz" \
	| grep -qx 'usr/share/hello/message.txt'

# shellcheck disable=SC2086
"$build/pkgbuild-example" $build_user_args --download \
	--helper "$root/libpkgbuild/pkgbuild-pkgfile.in" \
	--scanner "$build/pkgbuild-stage-scan" --fakeroot /usr/bin/fakeroot \
	--source-dir "$build/sources" \
	--work-dir "$build/archive-work" \
	--package-dir "$build/packages" \
	"$archive_fixture"

test -f "$build/packages/hello-archive#1.0-1.pkg.tar.gz"
tar -xOzf "$build/packages/hello-archive#1.0-1.pkg.tar.gz" \
	usr/share/hello-archive/message.txt \
	| grep -qx 'hello through curl and libarchive'

echo "libpkgbuild example: PASS"
