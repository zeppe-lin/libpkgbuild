project(
  'libpkgbuild',
  'cpp',
  version: '0.6.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.2.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

libarchive_dep = dependency('libarchive', required: true)
libcurl_dep = dependency('libcurl', required: true)
libcrypto_dep = dependency('libcrypto', required: true)
zlib_dep = dependency('zlib', required: true)

parity_option = get_option('parity')
libpkgimage_dep = dependency(
  'libpkgimage',
  version: '>=0.2.1',
  required: parity_option,
)

pkgfile_helper = join_paths(
  get_option('prefix'),
  get_option('libexecdir'),
  'pkgbuild-pkgfile',
)

cpp = meson.get_compiler('cpp')
helper_define = '-DPKGBUILD_PKGFILE_HELPER="' + pkgfile_helper + '"'
scanner_path = join_paths(get_option('prefix'), get_option('libexecdir'), 'pkgbuild-stage-scan')
scanner_define = '-DPKGBUILD_STAGE_SCANNER="' + scanner_path + '"'
strip_program = find_program('strip', required: true)
strip_define = '-DPKGBUILD_STRIP="' + strip_program.full_path() + '"'

inc = include_directories('include')

sources = files(
  'libpkgbuild/types.cpp',
  'libpkgbuild/source.cpp',
  'libpkgbuild/footprint.cpp',
  'libpkgbuild/process.cpp',
  'libpkgbuild/stage.cpp',
  'libpkgbuild/engine.cpp',
  'libpkgbuild/backends/pkgfile.cpp',
  'libpkgbuild/backends/openssl.cpp',
  'libpkgbuild/backends/normalize.cpp',
  'libpkgbuild/backends/curl.cpp',
  'libpkgbuild/backends/libarchive.cpp',
)

libpkgbuild = library(
  'pkgbuild',
  sources,
  include_directories: inc,
  dependencies: [libarchive_dep, libcurl_dep, libcrypto_dep, zlib_dep],
  version: meson.project_version(),
  soversion: '0',
  install: true,
)

pkgbuild_dep = declare_dependency(
  link_with: libpkgbuild,
  include_directories: inc,
  dependencies: [libarchive_dep, libcurl_dep, libcrypto_dep, zlib_dep],
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
  requires_private: ['libarchive', 'libcurl', 'libcrypto', 'zlib'],
)


stage_scanner = executable(
  'pkgbuild-stage-scan',
  'tools/pkgbuild-stage-scan.cpp',
  include_directories: [inc, include_directories('libpkgbuild')],
  dependencies: pkgbuild_dep,
  install: true,
  install_dir: get_option('libexecdir'),
)

executable(
  'pkgbuild-example',
  'tools/pkgbuild-example.cpp',
  include_directories: inc,
  dependencies: pkgbuild_dep,
  cpp_args: [helper_define, scanner_define, strip_define],
  install: false,
)

subdir('tests')


if libpkgimage_dep.found()
  parity_compare_sources = files(
    'tools/parity/archive_compare.cpp',
  )

  executable(
    'pkgbuild-archive-compare',
    ['tools/pkgbuild-archive-compare.cpp', parity_compare_sources],
    include_directories: include_directories('tools'),
    dependencies: [libpkgimage_dep, libcrypto_dep],
    install: false,
  )


  executable(
    'pkgbuild-parity',
    ['tools/pkgbuild-parity.cpp', parity_compare_sources],
    include_directories: include_directories('tools'),
    dependencies: [pkgbuild_dep, libpkgimage_dep, libcrypto_dep],
    cpp_args: [helper_define, scanner_define, strip_define],
    install: false,
  )
endif
