#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

srcdir=${1:-.}

require() {
    grep -F "$2" "$srcdir/$1" >/dev/null || {
        echo "$1: missing Meson topology contract: $2" >&2
        exit 1
    }
}

reject() {
    if grep -F "$2" "$srcdir/$1" >/dev/null; then
        echo "$1: forbidden Meson topology: $2" >&2
        exit 1
    fi
}

line_of() {
    grep -n -F "$2" "$srcdir/$1" | sed -n '1s/:.*//p'
}

require meson.build "pkgconfig = import('pkgconfig')"
require meson.build "pkgbuild_worker = configure_file("
require meson.build "pkgbuild_worker_build_path = join_paths("
require meson.build "pkgbuild_worker_install_path = join_paths("
require meson.build "stage_scanner_build_path = stage_scanner.full_path()"
require meson.build "stage_scanner_install_path = join_paths("
require meson.build "pkgsource_worker_path = libpkgsource_dep.get_variable("
require meson.build "internal: 'pkgfile_worker'"
require meson.build "pkgconfig: 'pkgfile_worker'"
require meson.build "if build_parity"
require meson.build "if build_planner_adapter"
require tests/meson.build "pkgbuild_worker,"
require tests/meson.build "'-DTEST_WORKER=\"' + pkgsource_worker_path + '\"'"
require tests/meson.build "if build_parity"
require tests/meson.build "if build_planner_adapter"
require tests/parity/run-parity-test.sh 'helper=$3'
require adapter/meson.build "'libpkgsource-plan >= 0.2.1'"

reject tests/meson.build "libpkgbuild/pkgbuild-pkgfile.in"
reject tests/meson.build "pkgfile_worker_build_path"
reject tests/meson.build "if libpkgimage_dep.found()"
reject tests/meson.build "is_variable('libpkgbuild_plan_dep')"
reject tests/parity/run-parity-test.sh 'helper=$root/../../libpkgbuild/pkgbuild-pkgfile.in'

pkgconfig_line=$(line_of meson.build "pkgconfig = import('pkgconfig')")
worker_line=$(line_of meson.build "pkgsource_worker_path = libpkgsource_dep.get_variable(")
adapter_line=$(line_of meson.build "subdir('adapter')")
tests_line=$(line_of meson.build "subdir('tests')")

[ -n "$pkgconfig_line" ] && [ -n "$worker_line" ] && \
    [ -n "$adapter_line" ] && [ -n "$tests_line" ] || {
    echo 'meson.build: cannot establish dependency/subdir ordering' >&2
    exit 1
}

[ "$pkgconfig_line" -lt "$adapter_line" ] || {
    echo 'meson.build: pkgconfig must be imported before adapter/meson.build' >&2
    exit 1
}

[ "$worker_line" -lt "$adapter_line" ] || {
    echo 'meson.build: source worker metadata must be resolved before adapter tests' >&2
    exit 1
}

[ "$adapter_line" -lt "$tests_line" ] || {
    echo 'meson.build: adapter targets must exist before tests are declared' >&2
    exit 1
}
