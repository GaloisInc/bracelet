tempfile_lib = static_library(
  'bracelet-tempfile',
  files([
    'tempfile.cpp',
  ]),
  include_directories: include_dir,
  dependencies: [boost, result, absl_log],
)
tempfile = declare_dependency(
  link_with: tempfile_lib,
  include_directories: include_dir,
)
