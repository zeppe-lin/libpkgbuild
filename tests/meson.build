# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
if get_option('tests').enabled()
  foreach item : [
    ['model-test', 'model_test.cpp'],
    ['request-test', 'request_test.cpp'],
    ['result-test', 'result_test.cpp'],
    ['public-headers', 'public_headers.cpp'],
  ]
    value = executable(
      item[0], item[1],
      include_directories: include_directories('.'),
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test(item[0], value)
  endforeach
endif
