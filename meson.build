# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
project(
  'libpkgbuild',
  'cpp',
  version: '1.0.0',
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
libcrypto_dep = dependency('libcrypto', required: true, static: want_static)
libpkgsource_dep = dependency(
  'libpkgsource',
  version: '>=1.0.0',
  required: true,
  static: want_static,
  fallback: ['libpkgsource', 'libpkgsource_dep'],
  default_options: [
    'default_library=' + library_kind,
    'link_mode=' + get_option('link_mode'),
  ],
)
libpkgsource_plan_dep = dependency(
  'libpkgsource-plan',
  version: '>=1.0.0',
  required: get_option('planner_adapter'),
  static: want_static,
  fallback: ['libpkgsource', 'libpkgsource_plan_dep'],
  default_options: [
    'default_library=' + library_kind,
    'link_mode=' + get_option('link_mode'),
    'planner_adapter=enabled',
  ],
)
libpkgimage_dep = dependency(
  'libpkgimage',
  version: '>=0.3.0',
  required: get_option('planner_adapter'),
  static: want_static,
  fallback: ['libpkgimage', 'libpkgimage_dep'],
  default_options: [
    'default_library=' + library_kind,
    'link_mode=' + get_option('link_mode'),
  ],
)
libpkgplan_dep = dependency(
  'libpkgplan',
  version: '>=0.2.0',
  required: get_option('planner_adapter'),
  static: want_static,
  fallback: ['libpkgplan', 'libpkgplan_dep'],
  default_options: [
    'default_library=' + library_kind,
    'link_mode=' + get_option('link_mode'),
  ],
)

subdir('src')
if libpkgsource_plan_dep.found() and libpkgimage_dep.found() and libpkgplan_dep.found()
  subdir('adapter')
endif
subdir('tests')
