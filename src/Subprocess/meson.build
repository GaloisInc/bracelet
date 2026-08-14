
subprocess_lib = static_library(
  'bracelet-subprocess',
  files([
    'Subprocess.cpp',
  ]),
  include_directories: include_dir,
  dependencies: [boost, result],
)
subprocess = declare_dependency(
  link_with: subprocess_lib,
  include_directories: include_dir,
  dependencies: [result],
)

test('bracelet subprocess test', executable(
  'subprocess-test',
  'Test.cpp',
  dependencies: [subprocess, catch2],
))

