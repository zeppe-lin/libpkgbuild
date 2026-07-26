# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
if get_option('tests').enabled()
  model_test = executable('model-test', 'model_test.cpp', dependencies: libpkgbuild_dep)
  test('model', model_test)
  public_headers = executable('public-headers', 'public_headers.cpp', dependencies: libpkgbuild_dep)
  test('public-headers', public_headers)
endif
