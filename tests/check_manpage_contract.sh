#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
srcdir=${1:-.}
page="$srcdir/man/pkgbuild_plan_adapter.3.scd"

grep -F '*pkgbuild::plan_adapter::artifact_projection*++' "$page" >/dev/null
grep -F '*pkgbuild::plan_adapter::project_artifact(* \' "$page" >/dev/null
grep -F '    *pkgbuild::BuildReceipt build,*++' "$page" >/dev/null
grep -F '    *const pkgimage::archive_backend& archives*);' "$page" >/dev/null

if grep -E '^[0-9]+\. ' "$srcdir"/man/*.scd >/dev/null; then
    echo 'ordered Markdown list syntax entered scdoc source' >&2
    exit 1
fi
