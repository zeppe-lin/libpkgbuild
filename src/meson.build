# SPDX-FileCopyrightText: 2026 Alexandr Savca
# SPDX-License-Identifier: GPL-3.0-or-later
public_inc = include_directories('../include')
private_inc = include_directories('.')
libpkgbuild = library(
  'pkgbuild',
  ['error.cpp', 'identity.cpp', 'identity_support.cpp', 'model.cpp'],
  include_directories: [public_inc, private_inc],
  dependencies: [libpkgsource_dep, libcrypto_dep],
  version: meson.project_version(),
  soversion: '2',
  install: true,
)
libpkgbuild_dep = declare_dependency(
  link_with: libpkgbuild,
  include_directories: public_inc,
  dependencies: [libpkgsource_dep, libcrypto_dep],
)
meson.override_dependency('libpkgbuild', libpkgbuild_dep)
install_headers(
  '../include/libpkgbuild/error.h',
  '../include/libpkgbuild/identity.h',
  '../include/libpkgbuild/libpkgbuild.h',
  '../include/libpkgbuild/model.h',
  subdir: 'libpkgbuild',
)
pkgconf.generate(
  libpkgbuild,
  name: 'libpkgbuild',
  filebase: 'libpkgbuild',
  description: 'Native package-build authority model',
  subdirs: '.',
  requires: ['libpkgsource >= 1.0.0'],
  requires_private: ['libcrypto'],
)
