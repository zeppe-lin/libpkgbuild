#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=${1:?source root required}
fail()
{
  echo "repository-contract: $*" >&2
  exit 1
}

for required in \
  meson.build \
  meson.options \
  abi/libpkgbuild.exports \
  include/libpkgbuild/libpkgbuild.h \
  src/meson.build \
  tests/meson.build
 do
  [ -s "$root/$required" ] || fail "missing or empty $required"
 done

[ ! -e "$root/meson_options.txt" ] ||
  fail 'legacy meson_options.txt name remains'

for directory in contracts fixtures header installed integration support unit
 do
  [ -d "$root/tests/$directory" ] || fail "missing tests/$directory"
 done

printf '%s\n' 'repository-contract: ok'
