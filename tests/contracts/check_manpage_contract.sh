#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
for page in \
    libpkgbuild.3.scdoc pkgbuild_model.3.scdoc pkgbuild_request.3.scdoc \
    pkgbuild_result.3.scdoc
do
    test -s "$root/man/$page"
    grep -F "['$page'" "$root/man/meson.build" >/dev/null
    if grep -n '\\$' "$root/man/$page" >/dev/null; then
        echo "obsolete scdoc continuation in man/$page" >&2
        exit 1
    fi
done
if grep -R -F 'pkgbuild_plan_adapter' "$root/man" >/dev/null 2>&1; then
    echo 'planner adapter documentation remains in core repository' >&2
    exit 1
fi
