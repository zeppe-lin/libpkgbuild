# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
project(
  'libpkgbuild',
  'cpp',
  version: '3.0.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.2.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
    'default_library=shared',
    'b_ndebug=if-release',
  ],
)

library_kind = get_option('default_library')
if library_kind == 'both'
  error('libpkgbuild requires separate shared and static builds')
endif
want_static = get_option('link_mode') == 'static'
if (library_kind == 'static') != want_static
  error('default_library and link_mode must both select shared or static')
endif

pkgconf = import('pkgconfig')
cpp = meson.get_compiler('cpp')
visibility_cpp_args = []
if cpp.has_argument('-fvisibility-inlines-hidden')
  visibility_cpp_args += ['-fvisibility-inlines-hidden']
endif

libcrypto_dep = dependency('libcrypto', required: true, static: want_static)
libpkgsource_dep = dependency(
  'libpkgsource',
  version: ['>=3.0.0', '<4.0.0'],
  required: true,
  static: want_static,
)
libpkgresolve_dep = dependency(
  'libpkgresolve',
  version: ['>=2.0.0', '<3.0.0'],
  required: true,
  static: want_static,
)

subdir('src')
subdir('man')
subdir('tests')
