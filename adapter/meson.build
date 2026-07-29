# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
adapter_inc = include_directories('../include')

libpkgbuild_plan = library(
  'pkgbuild-plan',
  'adapter.cpp',
  include_directories: adapter_inc,
  dependencies: [
    libpkgbuild_dep,
    libpkgsource_plan_dep,
    libpkgimage_dep,
    libpkgplan_dep,
    libcrypto_dep,
  ],
  version: meson.project_version(),
  soversion: '2',
  install: true,
)

libpkgbuild_plan_dep = declare_dependency(
  link_with: libpkgbuild_plan,
  include_directories: adapter_inc,
  dependencies: [
    libpkgbuild_dep,
    libpkgsource_plan_dep,
    libpkgimage_dep,
    libpkgplan_dep,
  ],
)
meson.override_dependency('libpkgbuild-plan', libpkgbuild_plan_dep)

install_headers(
  '../include/libpkgbuild-plan/adapter.h',
  subdir: 'libpkgbuild-plan',
)

pkgconf.generate(
  libpkgbuild_plan,
  name: 'libpkgbuild-plan',
  filebase: 'libpkgbuild-plan',
  description: 'Verified native build artifact projection into libpkgplan',
  subdirs: '.',
  requires: [
    'libpkgbuild = ' + meson.project_version(),
    'libpkgsource-plan >= 2.0.0',
    'libpkgimage >= 0.3.0',
    'libpkgplan >= 0.2.0',
  ],
  requires_private: ['libcrypto'],
)
