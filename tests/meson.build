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
