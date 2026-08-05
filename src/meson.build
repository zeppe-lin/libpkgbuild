# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
public_inc = include_directories('../include')
private_inc = include_directories('.')

elf_export_generator = find_program(
  meson.project_source_root() / 'tools/generate-elf-export-script.sh',
)
core_export_script_name = 'libpkgbuild.exports.map'
core_export_script_path = meson.current_build_dir() / core_export_script_name
core_export_script = configure_file(
  input: '../abi/libpkgbuild.exports',
  output: core_export_script_name,
  command: [elf_export_generator, '@INPUT@'],
  capture: true,
)

libpkgbuild = library(
  'pkgbuild',
  [
    'error.cpp',
    'identity.cpp',
    'identity_support.cpp',
    'model.cpp',
    'request.cpp',
    'result.cpp',
  ],
  include_directories: [public_inc, private_inc],
  dependencies: [libpkgsource_dep, libpkgresolve_dep, libcrypto_dep],
  cpp_args: visibility_cpp_args + ['-DPKGBUILD_BUILDING_LIBRARY'],
  gnu_symbol_visibility: 'hidden',
  link_args: ['-Wl,--version-script=' + core_export_script_path],
  link_depends: [core_export_script],
  version: meson.project_version(),
  soversion: '4',
  install: true,
)

libpkgbuild_consumer_deps = [libpkgsource_dep, libpkgresolve_dep]
if want_static
  libpkgbuild_consumer_deps += [libcrypto_dep]
endif

libpkgbuild_dep = declare_dependency(
  link_with: libpkgbuild,
  include_directories: public_inc,
  dependencies: libpkgbuild_consumer_deps,
  version: meson.project_version(),
)
meson.override_dependency('libpkgbuild', libpkgbuild_dep)

install_headers(
  '../include/libpkgbuild/error.h',
  '../include/libpkgbuild/export.h',
  '../include/libpkgbuild/identity.h',
  '../include/libpkgbuild/libpkgbuild.h',
  '../include/libpkgbuild/model.h',
  '../include/libpkgbuild/request.h',
  '../include/libpkgbuild/result.h',
  subdir: 'libpkgbuild',
)

pkgconf.generate(
  libpkgbuild,
  name: 'libpkgbuild',
  filebase: 'libpkgbuild',
  description: 'Native sealed package-build authority model',
  subdirs: '.',
  requires: [
    'libpkgsource >= 3.0.0',
    'libpkgsource < 4.0.0',
    'libpkgresolve >= 2.0.0',
    'libpkgresolve < 3.0.0',
  ],
  requires_private: ['libcrypto'],
)
