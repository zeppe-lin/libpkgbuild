#!/bin/sh
set -eu

candidate=$1
helper=$2
scanner=$3
fakeroot=$4
strip=$5

root=$(mktemp -d "${TMPDIR:-/tmp}/pkgbuild-reference.XXXXXX")
trap 'rm -rf "$root"' EXIT HUP INT TERM
chmod 0755 "$root"

recipe=$root/reference-tool
mkdir -p "$recipe"
cat > "$recipe/Pkgfile" <<'PKGFILE'
name=reference-tool
version=1.0
release=1
build() {
    mkdir -p "$PKG/usr/share/reference-tool"
    printf '%s\n' 'snapshot-bound payload' > \
        "$PKG/usr/share/reference-tool/value"
}
PKGFILE
chmod 0644 "$recipe/Pkgfile"

identity=
if [ "$(id -u)" -eq 0 ]; then
    identity="--build-user nobody"
fi

# Intentional splitting of the optional two-word identity argument.
# shellcheck disable=SC2086
output=$(
    "$candidate" \
        --source-dir "$root/sources" \
        --package-dir "$root/packages" \
        --work-dir "$root/work" \
        --tmp-dir "$root/tmp" \
        --helper "$helper" \
        --scanner "$scanner" \
        --fakeroot "$fakeroot" \
        --strip "$strip" \
        --no-strip \
        --no-compress-manpages \
        --write-package-path "$root/package.path" \
        $identity \
        "$recipe"
)

package=$(cat "$root/package.path")
case $package in
/*) ;;
*)
    echo "reference tool returned a relative package path" >&2
    exit 1
    ;;
esac

test -f "$package"
printf '%s\n' "$output" | awk -F '\t' -v package="$package" \
    '$1 == "artifact" && $2 == package { found=1 } END { exit !found }'
printf '%s\n' "$output" | awk -F '\t' \
    '$1 == "source-snapshot" && $2 ~ /^sha256:/ { found=1 } END { exit !found }'
printf '%s\n' "$output" | awk -F '\t' \
    '$1 == "package" && $2 == "reference-tool#1.0-1" { found=1 } END { exit !found }'
printf '%s\n' "$output" | awk -F '\t' \
    '$1 == "archive" && $2 == "gnutar/gz" { found=1 } END { exit !found }'
tar -tzf "$package" | grep -qx 'usr/share/reference-tool/value'

echo 'reference tool realization: PASS'
