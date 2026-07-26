#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
srcdir=${1:-.}

require() {
    grep -F "$2" "$srcdir/$1" >/dev/null || {
        echo "$1: missing release metadata: $2" >&2
        exit 1
    }
}

require meson.build "version: '0.9.0'"
require meson.build "soversion: '1'"
require adapter/meson.build "soversion: '0'"
require README.md 'LIBPKGBUILD 0.9.0'
require HISTORY.md '0.9.0'
require COPYRIGHT 'Copyright (C) 2026 Alexandr Savca'
require meson.build "license: 'GPL-3.0-or-later'"
