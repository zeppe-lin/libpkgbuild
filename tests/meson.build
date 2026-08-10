# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

if get_option('tests').allowed()
  unit_tests = {
    'identity': 'unit/identity_test.cpp',
    'policy': 'unit/policy_test.cpp',
    'payload-model': 'unit/payload_model_test.cpp',
  }

  integration_tests = {
    'architecture-binding': 'integration/architecture_binding_test.cpp',
    'input-authority': 'integration/input_authority_test.cpp',
    'request-authority': 'integration/request_authority_test.cpp',
    'request-identity': 'integration/request_identity_test.cpp',
    'result-semantics': 'integration/result_semantics_test.cpp',
  }

  foreach name, source : unit_tests
    executable_test = executable(
      name + '-test',
      source,
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test(name, executable_test, suite: 'unit')
  endforeach

  foreach name, source : integration_tests
    executable_test = executable(
      name + '-test',
      source,
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test(name, executable_test, suite: 'integration')
  endforeach

  public_headers = [
    'error.h',
    'export.h',
    'identity.h',
    'model.h',
    'request.h',
    'result.h',
    'libpkgbuild.h',
  ]
  foreach header : public_headers
    header_test = executable(
      'header-' + header.underscorify() + '-test',
      'header/public_header_test.cpp',
      cpp_args: [
        '-DPKGBUILD_TEST_HEADER=<libpkgbuild/' + header + '>',
      ],
      dependencies: libpkgbuild_dep,
      install: false,
    )
    test('header-' + header.underscorify(), header_test, suite: 'header')
  endforeach

  sh = find_program('sh')

  pkgconfig_metadata = find_program('contracts/check_pkgconfig_metadata.sh')
  test(
    'pkgconfig-metadata',
    pkgconfig_metadata,
    args: [meson.project_build_root()],
    suite: 'contract',
  )

  abi_layout = executable(
    'abi-layout-test',
    'contracts/abi_layout_test.cpp',
    dependencies: libpkgbuild_dep,
    install: false,
  )
  test('abi-layout', abi_layout, suite: 'contract')

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
      suite: 'contract',
    )
    dependency_abi = find_program('contracts/check_dependency_abi.sh')
    test(
      'dependency-abi',
      dependency_abi,
      args: [libpkgbuild.full_path()],
      suite: 'contract',
    )
  endif

  contract_tests = {
    'authority-contract': 'contracts/check_authority_contract.sh',
    'release-metadata': 'contracts/check_release_metadata.sh',
    'manpage-contract': 'contracts/check_manpage_contract.sh',
    'test-layout': 'contracts/check_test_layout.sh',
    'abi-contract': 'contracts/check_abi_contract.sh',
    'ci-contract': 'contracts/check_ci_contract.sh',
  }
  foreach name, program : contract_tests
    test(
      name,
      sh,
      args: [
        meson.current_source_dir() / program,
        meson.project_source_root(),
      ],
      suite: 'contract',
    )
  endforeach
endif
