project(
  'boost',
  'c', 'cpp',
  version : '1.89.0',
  meson_version : '>= 1.3.0',
  default_options : [
    'warning_level=3',
    'cpp_std=c++17',
    'cpp_rtti=false',
  ],
)

boost = declare_dependency(
  include_directories: [include_directories('.')],
)

