result_lib = static_library(
  'bracelet-result-lib',
  files('Result.cpp'),
  dependencies: [absl_strings, absl_debugging, boost, libbacktrace],
  cpp_args: libbacktrace.found() ? ['-DBRACELET_HAS_LIBBACKTRACE=1'] : [],
)

result = declare_dependency(
  dependencies: [absl_strings, boost],
  include_directories: include_dir,
  link_with: [result_lib]
)
