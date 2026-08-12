#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=${1:?}
meson=$root/tests/meson.build

for directory in contracts fixtures header installed integration support unit; do
  test -d "$root/tests/$directory" || {
    echo "missing test role directory: $directory" >&2
    exit 1
  }
done
for suite in unit integration header contract; do
  grep -F "suite: '$suite'" "$meson" >/dev/null || {
    echo "missing Meson test suite: $suite" >&2
    exit 1
  }
done
for stale in \
    fixture.h model_test.cpp request_test.cpp result_test.cpp public_headers.cpp public
do
  test ! -e "$root/tests/$stale" || {
    echo "flat test artifact remains: $stale" >&2
    exit 1
  }
done

for source in \
    integration/architecture_binding_test.cpp \
    integration/input_authority_test.cpp \
    integration/request_authority_test.cpp \
    integration/request_identity_test.cpp \
    integration/result_semantics_test.cpp
 do
  grep -F "'$source'" "$meson" >/dev/null || {
    echo "missing integration registration: $source" >&2
    exit 1
  }
done

grep -F "test('header-' + header.underscorify()" "$meson" >/dev/null
! grep -F "test('header:" "$meson" >/dev/null

for contract in "$root"/tests/contracts/*.sh; do
  test -x "$contract" || {
    echo "contract is not executable: ${contract##*/}" >&2
    exit 1
  }
  grep -F "${contract##*/}" "$meson" >/dev/null || {
    echo "contract is not registered: ${contract##*/}" >&2
    exit 1
  }
done
test -s "$root/tests/contracts/abi_layout_test.cpp" || {
  echo 'missing ABI layout contract' >&2
  exit 1
}
test -s "$root/tests/installed/consumer.cpp" || {
  echo 'missing installed-product consumer' >&2
  exit 1
}
