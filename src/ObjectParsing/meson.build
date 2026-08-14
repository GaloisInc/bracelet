object_parsing_lib = static_library(
  'bracelet-object-parsing',
  files([
    'StaticObject.cpp',
    'CoredumpObject.cpp',
    'ObjectParsing.cpp',
  ]),
  include_directories: include_dir,
  dependencies: [boost, lldb, llvm, result, absl_container, absl_log],
)
object_parsing = declare_dependency(
  link_with: object_parsing_lib,
  include_directories: include_dir,
  dependencies: [boost, result, absl_container, llvm],
)

object_parsing_testcase1 = executable(
  'object_parsing_testcase1',
  'test-cases/test1.c',
)

object_parsing_testcase2 = executable(
  'object_parsing_testcase2',
  'test-cases/test2.cpp',
)

object_parsing_testcase3 = executable(
  'object_parsing_testcase3',
  'test-cases/test3.c',
)

test(
  'object parsing test',
  executable(
    'object_parsing_test',
    'Test.cpp',
    c_args: ['-fPIC', '-fPIE'],
    dependencies: [
      object_parsing,
      catch2,
      tempfile,
      subprocess,
    ],
  ),
)

