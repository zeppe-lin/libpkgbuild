#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

worker=$1
scanner=$2
worker_source=$3

[ -x "$worker" ] || {
    echo "installed pkgbuild worker is missing or not executable: $worker" >&2
    exit 1
}

[ -x "$scanner" ] || {
    echo "installed stage scanner is missing or not executable: $scanner" >&2
    exit 1
}

cmp "$worker_source" "$worker" || {
    echo 'installed pkgbuild worker differs from its configured input' >&2
    exit 1
}
