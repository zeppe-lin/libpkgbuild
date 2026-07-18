#!/bin/sh
set -eu

package_dir=
work_base=
config=
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
    --config)
        config=$2
        shift 2
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
if [ -n "$config" ]; then
    # shellcheck disable=SC1090
    . "$config"
fi
if [ -f "$recipe/require-baseline" ] && \
   [ "${PARITY_BASELINE_MARKER:-}" != yes ]; then
    echo "fake-pkgbuild: baseline configuration was not sourced" >&2
    exit 2
fi
if [ -f "$recipe/require-download" ] && [ "$download" != yes ]; then
    echo "fake-pkgbuild: download mode was not forwarded" >&2
    exit 2
fi

workspace=$work_base/.pkgbuild.fixture
mkdir -p "$workspace/tmp"
if [ -f "$recipe/candidate-build-fail" ]; then
    echo "candidate fixture stdout"
    echo "candidate fixture stderr" >&2
    exit 9
fi

tree=$workspace/fake-root
rm -rf "$tree"
mkdir -p "$tree/usr/share/parity"
if [ -f "$recipe/candidate-different" ]; then
    printf '%s\n' 'different payload' > "$tree/usr/share/parity/value"
else
    printf '%s\n' 'same payload' > "$tree/usr/share/parity/value"
fi
printf '%s\n' "$workspace" > "$tree/usr/share/parity/workspace"
package=$name#$version-$release.pkg.tar.gz
if [ -f "$recipe/candidate-name-different" ]; then
    package=other#$version-$release.pkg.tar.gz
fi
tar --owner=0 --group=0 -C "$tree" -czf "$package_dir/$package" usr
