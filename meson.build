project(
  'libpkgbuild',
  'cpp',
  version: '0.9.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>=1.2.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

pkgconfig = import('pkgconfig')
cpp = meson.get_compiler('cpp')

planner_adapter_option = get_option('planner_adapter')
parity_option = get_option('parity')

libpkgsource_dep = dependency(
  'libpkgsource',
  version: '>=0.1.0',
  required: true,
)
libpkgsource_plan_dep = dependency(
  'libpkgsource-plan',
  version: '>=0.2.1',
  required: planner_adapter_option,
)
libpkgplan_dep = dependency(
  'libpkgplan',
  version: '>=0.2.0',
  required: planner_adapter_option,
)
libarchive_dep = dependency('libarchive', required: true)
libcurl_dep = dependency('libcurl', required: true)
libcrypto_dep = dependency('libcrypto', required: true)
zlib_dep = dependency('zlib', required: true)
libpkgimage_dep = dependency(
  'libpkgimage',
  version: '>=0.3.0',
  required: parity_option.enabled() or planner_adapter_option.enabled(),
)

build_planner_adapter = planner_adapter_option.enabled() or (
  planner_adapter_option.auto() and
  libpkgsource_plan_dep.found() and
  libpkgplan_dep.found() and
  libpkgimage_dep.found()
)
build_parity = parity_option.enabled() or (
  parity_option.auto() and libpkgimage_dep.found()
)

prefix = get_option('prefix')
libexecdir = get_option('libexecdir')
installed_libexecdir = join_paths(prefix, libexecdir)

pkgbuild_worker_name = 'pkgbuild-pkgfile'
pkgbuild_worker_build_path = join_paths(
  meson.current_build_dir(),
  pkgbuild_worker_name,
)
pkgbuild_worker_install_path = join_paths(
  installed_libexecdir,
  pkgbuild_worker_name,
)

pkgbuild_worker = configure_file(
  input: 'libpkgbuild/pkgbuild-pkgfile.in',
  output: pkgbuild_worker_name,
  copy: true,
  install: true,
  install_dir: libexecdir,
  install_mode: 'rwxr-xr-x',
)
chmod_program = find_program('chmod', required: true)
run_command(
  chmod_program,
  '0755',
  pkgbuild_worker_build_path,
  check: true,
)

strip_program = find_program('strip', required: true)
ar_program = find_program('ar', required: true)
strip_define = '-DPKGBUILD_STRIP="' + strip_program.full_path() + '"'

inc = include_directories('include')

sources = files(
  'libpkgbuild/types.cpp',
  'libpkgbuild/definition.cpp',
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
  dependencies: [
    libpkgsource_dep,
    libarchive_dep,
    libcurl_dep,
    libcrypto_dep,
    zlib_dep,
  ],
  version: meson.project_version(),
  soversion: '1',
  install: true,
)

pkgbuild_dep = declare_dependency(
  link_with: libpkgbuild,
  include_directories: inc,
  dependencies: [
    libpkgsource_dep,
    libarchive_dep,
    libcurl_dep,
    libcrypto_dep,
    zlib_dep,
  ],
)

meson.override_dependency('libpkgbuild', pkgbuild_dep)

install_subdir(
  'include/pkgbuild',
  install_dir: get_option('includedir'),
)

pkgconfig.generate(
  libpkgbuild,
  name: 'libpkgbuild',
  description: 'Zeppe-Lin package build engine',
  version: meson.project_version(),
  requires: ['libpkgsource >= 0.1.0'],
  requires_private: ['libarchive', 'libcurl', 'libcrypto', 'zlib'],
)

stage_scanner_name = 'pkgbuild-stage-scan'
stage_scanner = executable(
  stage_scanner_name,
  'tools/pkgbuild-stage-scan.cpp',
  include_directories: [inc, include_directories('libpkgbuild')],
  dependencies: pkgbuild_dep,
  install: true,
  install_dir: libexecdir,
)
stage_scanner_build_path = stage_scanner.full_path()
stage_scanner_install_path = join_paths(
  installed_libexecdir,
  stage_scanner_name,
)

helper_define = (
  '-DPKGBUILD_PKGFILE_HELPER="' + pkgbuild_worker_build_path + '"'
)
scanner_define = (
  '-DPKGBUILD_STAGE_SCANNER="' + stage_scanner_build_path + '"'
)

pkgbuild_example = executable(
  'pkgbuild-example',
  'tools/pkgbuild-example.cpp',
  include_directories: inc,
  dependencies: pkgbuild_dep,
  cpp_args: [helper_define, scanner_define, strip_define],
  install: false,
)

if build_parity
  parity_compare_sources = files(
    'tools/parity/archive_compare.cpp',
  )

  executable(
    'pkgbuild-archive-compare',
    ['tools/pkgbuild-archive-compare.cpp', parity_compare_sources],
    include_directories: include_directories('tools'),
    dependencies: [libpkgimage_dep, libcrypto_dep, zlib_dep],
    install: false,
  )

  pkgbuild_parity = executable(
    'pkgbuild-parity',
    ['tools/pkgbuild-parity.cpp', parity_compare_sources],
    include_directories: include_directories('tools'),
    dependencies: [pkgbuild_dep, libpkgimage_dep, libcrypto_dep, zlib_dep],
    cpp_args: [helper_define, scanner_define, strip_define],
    install: false,
  )
endif

if build_planner_adapter
  pkgsource_worker_path = libpkgsource_dep.get_variable(
    internal: 'pkgfile_worker',
    pkgconfig: 'pkgfile_worker',
  )
  subdir('adapter')
endif

subdir('tests')
subdir('man')
