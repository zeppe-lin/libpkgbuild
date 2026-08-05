# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
if get_option('tests').enabled()
  behavior_tests = {
    'model': 'model_test.cpp',
    'request': 'request_test.cpp',
    'result': 'result_test.cpp',
  }
  foreach test_name, source_file : behavior_tests
    executable_target = executable(
      test_name + '-test',
      source_file,
      include_directories: include_directories('.'),
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test(test_name, executable_target)
  endforeach

  public_headers = {
    'public-umbrella': 'public_headers.cpp',
    'public-error': 'public/error_header_test.cpp',
    'public-export': 'public/export_header_test.cpp',
    'public-identity': 'public/identity_header_test.cpp',
    'public-model': 'public/model_header_test.cpp',
    'public-request': 'public/request_header_test.cpp',
    'public-result': 'public/result_header_test.cpp',
  }
  foreach test_name, source_file : public_headers
    executable_target = executable(
      test_name,
      source_file,
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test(test_name, executable_target)
  endforeach

  sh = find_program('sh')
  if library_kind == 'shared'
    nm = find_program('nm')
    test(
      'abi-surface',
      sh,
      args: [
        meson.current_source_dir() / 'contracts/check_abi_surface.sh',
        nm.full_path(),
        libpkgbuild.full_path(),
        meson.project_source_root() / 'abi/libpkgbuild.exports',
      ],
    )
  endif

  foreach contract : [
    'authority-contract',
    'release-metadata',
    'manpage-contract',
    'test-structure',
  ]
    test(
      contract,
      sh,
      args: [
        meson.current_source_dir() / ('contracts/check_' + contract.replace('-', '_') + '.sh'),
        meson.project_source_root(),
      ],
    )
  endforeach
endif
