#!/bin/sh
# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
root=$1
for file in \
    model_test.cpp request_test.cpp result_test.cpp public_headers.cpp \
    public/error_header_test.cpp public/export_header_test.cpp \
    public/identity_header_test.cpp public/model_header_test.cpp \
    public/request_header_test.cpp public/result_header_test.cpp
do
    test -s "$root/tests/$file"
done
test -s "$root/abi/libpkgbuild.exports"
test -x "$root/tools/generate-elf-export-script.sh"
test -x "$root/tests/contracts/check_abi_surface.sh"
