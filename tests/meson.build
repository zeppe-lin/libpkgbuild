process_executor_test = executable(
  'process-executor-test',
  'process-executor-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('process executor', process_executor_test)

workspace_test = executable(
  'workspace-test',
  'workspace-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('private workspace', workspace_test)

archive_integrity_test = executable(
  'archive-integrity-test',
  'archive-integrity-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('staged archive integrity', archive_integrity_test)

fakeroot_program = find_program('fakeroot', required: true)
fakeroot_metadata_test = executable(
  'fakeroot-metadata-test',
  'fakeroot-metadata-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test(
  'fakeroot staged metadata',
  fakeroot_metadata_test,
  args: [
    join_paths(meson.project_source_root(), 'libpkgbuild/pkgbuild-pkgfile.in'),
    stage_scanner,
    fakeroot_program.full_path(),
  ],
)

source_verifier_test = executable(
  'source-verifier-test',
  'source-verifier-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('source verifier', source_verifier_test)

pkgfile_checksum_test = executable(
  'pkgfile-checksum-test',
  'pkgfile-checksum-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test(
  'Pkgfile checksum manifest',
  pkgfile_checksum_test,
  args: [
    join_paths(meson.project_source_root(), 'libpkgbuild/pkgbuild-pkgfile.in'),
  ],
)

source_integrity_test = executable(
  'source-integrity-test',
  'source-integrity-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('source integrity pipeline', source_integrity_test)

manpage_transform_test = executable(
  'manpage-transform-test',
  'manpage-transform-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('man page transformation', manpage_transform_test)

strip_transform_test = executable(
  'strip-transform-test',
  'strip-transform-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test(
  'binary transformation',
  strip_transform_test,
  args: [strip_program.full_path()],
)

normalization_pipeline_test = executable(
  'normalization-pipeline-test',
  'normalization-pipeline-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test(
  'package normalization pipeline',
  normalization_pipeline_test,
  args: [
    join_paths(meson.project_source_root(), 'libpkgbuild/pkgbuild-pkgfile.in'),
    stage_scanner,
    fakeroot_program.full_path(),
    strip_program.full_path(),
  ],
)
