# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later

plan_adapter_inc = include_directories('../include')

libpkgbuild_plan = library(
  'pkgbuild-plan',
  'adapter.cpp',
  include_directories: plan_adapter_inc,
  dependencies: [
    pkgbuild_dep,
    libpkgsource_plan_dep,
    libpkgimage_dep,
    libpkgplan_dep,
    libcrypto_dep,
  ],
  install: true,
  version: meson.project_version(),
  soversion: '0',
)

libpkgbuild_plan_dep = declare_dependency(
  link_with: libpkgbuild_plan,
  include_directories: plan_adapter_inc,
  dependencies: [
    pkgbuild_dep,
    libpkgsource_plan_dep,
    libpkgimage_dep,
    libpkgplan_dep,
  ],
)

meson.override_dependency('libpkgbuild-plan', libpkgbuild_plan_dep)

install_headers(
  '../include/pkgbuild-plan/adapter.hpp',
  subdir: 'pkgbuild-plan',
)

pkgconfig.generate(
  libpkgbuild_plan,
  name: 'libpkgbuild-plan',
  filebase: 'libpkgbuild-plan',
  description: 'Explicit libpkgbuild to libpkgplan artifact adapter',
  subdirs: '.',
  requires: [
    'libpkgbuild = ' + meson.project_version(),
    'libpkgsource-plan >= 0.2.1',
    'libpkgimage >= 0.3.0',
    'libpkgplan >= 0.2.0',
  ],
  requires_private: ['libcrypto'],
)
