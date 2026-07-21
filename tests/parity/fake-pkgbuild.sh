#!/bin/sh
set -eu

if [ "${LANG:-}" != C.UTF-8 ] || [ "${LC_ALL:-}" != C.UTF-8 ]; then
    echo "fake-pkgbuild: parity locale is not C.UTF-8" >&2
    exit 2
fi

package_dir=
work_base=
workspace_dir=
tmp_dir=
package_path_output=
archive_format=
compression=
strip_binaries=yes
compress_manpages=yes
download=no
recipe=
while [ "$#" -gt 0 ]; do
    case $1 in
    --package-dir)
        package_dir=$2
        shift 2
        ;;
    --work-dir)
        work_base=$2
        shift 2
        ;;
    --workspace-dir)
        workspace_dir=$2
        shift 2
        ;;
    --tmp-dir)
        tmp_dir=$2
        shift 2
        ;;
    --write-package-path)
        package_path_output=$2
        shift 2
        ;;
    --archive-format)
        archive_format=$2
        shift 2
        ;;
    --compression)
        compression=$2
        shift 2
        ;;
    --no-strip)
        strip_binaries=no
        shift
        ;;
    --no-compress-manpages)
        compress_manpages=no
        shift
        ;;
    --source-dir|--helper|--scanner|--fakeroot|--strip)
        shift 2
        ;;
    --download)
        download=yes
        shift
        ;;
    --keep-work)
        shift
        ;;
    *)
        recipe=$1
        shift
        ;;
    esac
done

package_name=$(basename "$recipe")
# Fixture Pkgfiles contain data assignments and an inert build function.
# shellcheck disable=SC1090
. "$recipe/Pkgfile"
if [ "$package_name" != "$name" ]; then
    echo "fake-pkgbuild: recipe basename is not package name" >&2
    exit 2
fi
if [ -f "$recipe/require-download" ] && [ "$download" != yes ]; then
    echo "fake-pkgbuild: download mode was not forwarded" >&2
    exit 2
fi
[ -n "$package_path_output" ] || {
    echo "fake-pkgbuild: package-path output was not requested" >&2
    exit 2
}
if [ -f "$recipe/require-policy" ] &&
   { [ "$archive_format" != pax ] ||
     [ "$compression" != xz ] ||
     [ "$strip_binaries" != no ] ||
     [ "$compress_manpages" != no ]; }; then
    echo "fake-pkgbuild: explicit candidate policy was not forwarded" >&2
    exit 2
fi

if [ -n "$workspace_dir" ]; then
    workspace=$workspace_dir
else
    workspace=$work_base/.pkgbuild.fixture
fi
mkdir -p "$workspace" "$tmp_dir"
if [ -f "$recipe/candidate-build-fail" ]; then
    echo "candidate fixture line 1"
    echo "candidate fixture line 2"
    echo "candidate fixture line 3"
    echo "candidate fixture stdout"
    echo "candidate fixture stderr" >&2
    exit 9
fi

tree=$workspace/fake-root
rm -rf "$tree"
mkdir -p "$tree/usr/share/parity"
if [ -f "$recipe/candidate-unstable" ]; then
    count_file=$recipe/.candidate-run-count
    count=0
    if [ -f "$count_file" ]; then
        count=$(cat "$count_file")
    fi
    count=$((count + 1))
    printf '%s\n' "$count" > "$count_file"
    printf 'candidate run %s\n' "$count" > "$tree/usr/share/parity/value"
elif [ -f "$recipe/candidate-different" ]; then
    printf '%s\n' 'different payload' > "$tree/usr/share/parity/value"
else
    printf '%s\n' 'same payload' > "$tree/usr/share/parity/value"
fi
if [ -f "$recipe/candidate-many-differences" ]; then
    for number in 1 2 3 4 5; do
        printf 'candidate detail %s\n' "$number" > \
            "$tree/usr/share/parity/extra-$number"
    done
fi
printf '%s\n' "$workspace" > "$tree/usr/share/parity/workspace"
printf '%s\n' "$tmp_dir" > "$tree/usr/share/parity/tmpdir"
if [ -f "$recipe/candidate-artifact-fail" ]; then
    mkdir -p "$tree/usr/lib"
    printf '!<thin>\n' > "$tree/usr/lib/libfixture.a"
fi
package=$name#$version-$release.pkg.tar.gz
if [ -f "$recipe/candidate-name-different" ]; then
    package=other#$version-$release.pkg.tar.gz
fi
tar --owner=0 --group=0 -C "$tree" -czf "$package_dir/$package" usr
printf '%s\n' "$package_dir/$package" > "$package_path_output"
