project(
  'libpkgbuild',
  'cpp',
  version: '0.2.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.2.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

libarchive_dep = dependency('libarchive', required: true)
libcurl_dep = dependency('libcurl', required: true)

pkgfile_helper = join_paths(
  get_option('prefix'),
  get_option('libexecdir'),
  'pkgbuild-pkgfile',
)

cpp = meson.get_compiler('cpp')
helper_define = '-DPKGBUILD_PKGFILE_HELPER="' + pkgfile_helper + '"'

inc = include_directories('include')

sources = files(
  'libpkgbuild/types.cpp',
  'libpkgbuild/process.cpp',
  'libpkgbuild/engine.cpp',
  'libpkgbuild/backends/pkgfile.cpp',
  'libpkgbuild/backends/curl.cpp',
  'libpkgbuild/backends/libarchive.cpp',
)

libpkgbuild = library(
  'pkgbuild',
  sources,
  include_directories: inc,
  dependencies: [libarchive_dep, libcurl_dep],
  version: meson.project_version(),
  soversion: '0',
  install: true,
)

pkgbuild_dep = declare_dependency(
  link_with: libpkgbuild,
  include_directories: inc,
  dependencies: [libarchive_dep, libcurl_dep],
)

meson.override_dependency('libpkgbuild', pkgbuild_dep)

install_subdir(
  'include/pkgbuild',
  install_dir: get_option('includedir'),
)

install_data(
  'libpkgbuild/pkgbuild-pkgfile.in',
  install_dir: get_option('libexecdir'),
  rename: 'pkgbuild-pkgfile',
  install_mode: 'rwxr-xr-x',
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(
  libpkgbuild,
  name: 'libpkgbuild',
  description: 'Zeppe-Lin package build engine',
  version: meson.project_version(),
  requires_private: ['libarchive', 'libcurl'],
)

executable(
  'pkgbuild-example',
  'tools/pkgbuild-example.cpp',
  include_directories: inc,
  dependencies: pkgbuild_dep,
  cpp_args: helper_define,
  install: false,
)

subdir('tests')
