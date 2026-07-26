process_executor_test = executable(
  'process-executor-test',
  'process-executor-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('process executor', process_executor_test)

pkgfile_paths_test = find_program('pkgfile-paths-test.sh')
test(
  'Pkgfile execution paths',
  pkgfile_paths_test,
  args: [
    join_paths(meson.project_source_root(), 'libpkgbuild/pkgbuild-pkgfile.in'),
  ],
)

pkgfile_captured_run_test = find_program('pkgfile-captured-run-test.sh')
test(
  'captured Pkgfile execution',
  pkgfile_captured_run_test,
  args: [
    join_paths(meson.project_source_root(), 'libpkgbuild/pkgbuild-pkgfile.in'),
  ],
)

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

source_extraction_test = executable(
  'source-extraction-test',
  'source-extraction-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('source archive extraction', source_extraction_test)

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

archive_strip_ownership_test = executable(
  'archive-strip-ownership-test',
  'archive-strip-ownership-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test(
  'archive strip ownership',
  archive_strip_ownership_test,
  args: [strip_program.full_path(), ar_program.full_path()],
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

footprint_test = executable(
  'footprint-test',
  'footprint-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('footprint model', footprint_test)

footprint_pipeline_test = executable(
  'footprint-pipeline-test',
  'footprint-pipeline-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('footprint pipeline', footprint_pipeline_test)


if libpkgimage_dep.found()
  parity_compare_test = executable(
    'parity-compare-test',
    [
      'parity-compare-test.cpp',
      '../tools/parity/archive_compare.cpp',
    ],
    include_directories: include_directories('../tools'),
    dependencies: [pkgbuild_dep, libpkgimage_dep, libcrypto_dep, zlib_dep],
    install: false,
  )

  test('semantic archive comparison', parity_compare_test)

  parity_runner_test = find_program('parity/run-parity-test.sh')
  test(
    'pkgmk parity corpus runner',
    parity_runner_test,
    args: [pkgbuild_parity, fakeroot_program.full_path()],
  )
endif

source_projection_test = executable(
  'source-projection-test',
  'source-projection-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('source snapshot projection', source_projection_test)

definition_engine_test = executable(
  'definition-engine-test',
  'definition-engine-test.cpp',
  dependencies: pkgbuild_dep,
  install: false,
)

test('sealed build definition engine', definition_engine_test)

reference_tool_test = find_program('reference-tool-test.sh')
test(
  'reference tool realization',
  reference_tool_test,
  args: [
    pkgbuild_example,
    join_paths(meson.project_source_root(), 'libpkgbuild/pkgbuild-pkgfile.in'),
    stage_scanner,
    fakeroot_program.full_path(),
    strip_program.full_path(),
  ],
)

if is_variable('libpkgbuild_plan_dep')
  plan_adapter_test = executable(
    'plan-adapter-test',
    'plan-adapter-test.cpp',
    dependencies: libpkgbuild_plan_dep,
    cpp_args: [
      '-DTEST_WORKER="' + pkgfile_worker_build_path + '"',
    ],
    install: false,
  )
  test('sealed build planner projection', plan_adapter_test)

  plan_adapter_header_test = executable(
    'plan-adapter-header-test',
    'plan-adapter-header-test.cpp',
    dependencies: libpkgbuild_plan_dep,
    install: false,
  )
  test('planner adapter public header', plan_adapter_header_test)
endif

adapter_contract_test = find_program('check_adapter_contract.sh')
test(
  'planner adapter source contract',
  adapter_contract_test,
  args: [meson.project_source_root()],
)

manpage_contract_test = find_program('check_manpage_contract.sh')
test(
  'manual source contract',
  manpage_contract_test,
  args: [meson.project_source_root()],
)

release_metadata_test = find_program('check_release_metadata.sh')
test(
  'release metadata',
  release_metadata_test,
  args: [meson.project_source_root()],
)
