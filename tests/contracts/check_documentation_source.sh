#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu

root=${1:?source root required}
fail()
{
  echo "documentation-source-contract: $*" >&2
  exit 1
}

for document in README.md DESIGN.md TESTING.md MAINTAINING.md HISTORY.md
 do
  [ -s "$root/$document" ] || fail "missing or empty $document"
  case $(sed -n '1p' "$root/$document") in
    '# '*) ;;
    *) fail "$document does not start with an ATX level-one heading" ;;
  esac
 done

if grep -RInE '^[-=]{3,}$' "$root"/*.md >/dev/null; then
  fail 'Setext Markdown heading remains in root documentation'
fi

for heading in \
  '## Authority position' \
  '## Build subject' \
  '## Direct build inputs' \
  '## Source materialization' \
  '## Architecture and policy' \
  '## Build request identity' \
  '## Build result' \
  '## ABI discipline' \
  '## Non-goals'
 do
  grep -F "$heading" "$root/DESIGN.md" >/dev/null ||
    fail "DESIGN.md is missing ATX heading: $heading"
 done

printf '%s\n' 'documentation-source-contract: ok'
