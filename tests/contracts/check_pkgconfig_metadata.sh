#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
build_root=${1:?build root required}
metadata=$build_root/meson-private/libpkgbuild.pc
fail()
{
    echo "metadata-test: $*" >&2
    if test -n "${metadata:-}" && test -f "$metadata"; then
        echo '--- generated metadata ---' >&2
        cat "$metadata" >&2
        echo '--- end generated metadata ---' >&2
    fi
    exit 1
}
if test ! -s "$metadata"; then
    metadata=$(find "$build_root" -type f -name libpkgbuild.pc -print | sed -n '1p')
fi
test -n "${metadata:-}" && test -s "$metadata" ||
    fail 'generated libpkgbuild.pc was not found'
name=$(sed -n 's/^Name:[[:space:]]*//p' "$metadata")
test "$name" = libpkgbuild || fail "module name is '$name'"
version=$(sed -n 's/^Version:[[:space:]]*//p' "$metadata")
test "$version" = 3.0.3 || fail "module version is '$version'"
normalize_requirements()
{
    sed \
      -e 's/^[[:space:]]*//' \
      -e 's/[[:space:]]*$//' \
      -e 's/[[:space:]][[:space:]]*/ /g' \
      -e 's/ *\([<>]=\|[<>=]\) */ \1 /' \
      -e '/^$/d'
}
requires=$(sed -n 's/^Requires:[[:space:]]*//p' "$metadata" |
    tr ',' '\n' | normalize_requirements)
expected='libpkgsource >= 4.0.0
libpkgsource < 5.0.0
libpkgresolve >= 4.0.0
libpkgresolve < 5.0.0'
for requirement in \
    'libpkgsource >= 4.0.0' 'libpkgsource < 5.0.0' \
    'libpkgresolve >= 4.0.0' 'libpkgresolve < 5.0.0'
do
    count=$(printf '%s\n' "$requires" | grep -Fxc "$requirement" || true)
    test "$count" -eq 1 ||
        fail "metadata contains $count copies of '$requirement', expected exactly one"
done
test "$(printf '%s\n' "$requires" | LC_ALL=C sort)" = \
     "$(printf '%s\n' "$expected" | LC_ALL=C sort)" ||
    fail 'public requirements are not the exact build dependency intervals'
requires_private=$(sed -n 's/^Requires\.private:[[:space:]]*//p' "$metadata" |
    tr ',' '\n' | normalize_requirements)
test "$requires_private" = libcrypto ||
    fail "private requirements are '$requires_private', expected libcrypto"
libs=$(sed -n 's/^Libs:[[:space:]]*//p' "$metadata")
printf ' %s \n' "$libs" | grep -F ' -lpkgbuild ' >/dev/null ||
    fail 'metadata omits -lpkgbuild'
