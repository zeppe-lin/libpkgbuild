#!/bin/sh
set -eu

package_dir=
recipe=
while [ "$#" -gt 0 ]; do
    case $1 in
    --package-dir)
        package_dir=$2
        shift 2
        ;;
    --source-dir|--work-dir|--config|--helper|--scanner|--fakeroot|--strip)
        shift 2
        ;;
    --download|--keep-work)
        shift
        ;;
    *)
        recipe=$1
        shift
        ;;
    esac
done

if [ "$(basename "$recipe")" != fake ]; then
    echo "fake-pkgbuild: recipe basename is not package name" >&2
    exit 2
fi

tree=$package_dir/fake-root
rm -rf "$tree"
mkdir -p "$tree/usr/share/parity"
if [ -f "$recipe/candidate-different" ]; then
    printf '%s\n' 'different payload' > "$tree/usr/share/parity/value"
else
    printf '%s\n' 'same payload' > "$tree/usr/share/parity/value"
fi
package=fake#1.0-1.pkg.tar.gz
if [ -f "$recipe/candidate-name-different" ]; then
    package=other#1.0-1.pkg.tar.gz
fi
tar --owner=0 --group=0 -C "$tree" -czf "$package_dir/$package" usr
