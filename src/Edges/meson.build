edges_reader_lib = static_library(
  'bracelet-edges-reader',
  files([
    'Reader.cpp',
  ]),
  include_directories: include_dir,
  dependencies: [boost, streamvbyte, object_parsing, result, absl_log, zstd],
)
edges_reader = declare_dependency(
  link_with: edges_reader_lib,
  include_directories: include_dir,
  dependencies: [object_parsing, result],
)


edges_writer_lib = static_library(
  'bracelet-edges-writer',
  files([
    'Writer.cpp',
  ]),
  include_directories: include_dir,
  dependencies: [boost, streamvbyte, llvm, absl_container, zstd, result],
)
edges_writer = declare_dependency(
  link_with: edges_writer_lib,
  include_directories: include_dir,
  dependencies: [boost, result],
)

test(
  'edge encoding test',
  executable(
    'edges-encoding-test',
    files(['Tests.cpp']),
    dependencies: [
      absl_container,
      boost,
      catch2,
      edges_reader,
      edges_writer,
      llvm,
      llvm_with_codegen,
      result,
      object_parsing,
      tempfile,
      subprocess,
    ],
  ),
  timeout: 120,
)

