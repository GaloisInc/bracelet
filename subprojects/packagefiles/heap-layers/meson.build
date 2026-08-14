project(
  'heap-layers',
  'c', 'cpp',
  version : '1.0.0-1',
  meson_version : '>= 1.3.0',
  default_options : [
    'warning_level=3',
    'cpp_std=c++17',
    'cpp_rtti=false',
  ],
)

heap_layers = declare_dependency(
  include_directories: [include_directories('.')],
  sources: files(
    'wrappers/gnuwrapper.cpp',
  ),
)

