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
      item[0],
      item[1],
      include_directories: include_directories('.'),
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test(item[0], value)
  endforeach

  if libpkgsource_plan_dep.found() and libpkgimage_dep.found() and libpkgplan_dep.found()
    plan_adapter_test = executable(
      'plan-adapter-test',
      'plan_adapter_test.cpp',
      include_directories: include_directories('.'),
      dependencies: libpkgbuild_plan_dep,
      install: false,
    )
    test('plan-adapter', plan_adapter_test)

    plan_adapter_header_test = executable(
      'plan-adapter-header-test',
      'plan_adapter_header_test.cpp',
      dependencies: libpkgbuild_plan_dep,
      install: false,
    )
    test('plan-adapter-header', plan_adapter_header_test)
  endif

endif
