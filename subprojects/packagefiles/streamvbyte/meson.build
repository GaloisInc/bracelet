project(
  'streamvbyte',
  'c', 'cpp',
  version : '2.0.0-1',
  meson_version : '>= 1.3.0',
  default_options : [
    'warning_level=3',
  ],
)

streamvbyte_lib = static_library(
  'streamvbyte',
  files([
    'src/streamvbyte_decode.c',
    'src/streamvbyte_encode.c',
    'src/streamvbytedelta_decode.c',
    'src/streamvbytedelta_encode.c',
    'src/streamvbyte_0124_encode.c',
    'src/streamvbyte_0124_decode.c',
    'src/streamvbyte_zigzag.c',
  ]),
  include_directories: [include_directories('include')]
)

streamvbyte = declare_dependency(
  include_directories: [include_directories('include')],
  link_with: [streamvbyte_lib],
)

