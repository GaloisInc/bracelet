project(
  'bracelet-reachability',
  'c', 'cpp',
  version : '0.1',
  meson_version : '>= 1.7.2',
  default_options : [
    'warning_level=1',
    'cpp_std=c++17',
    'cpp_rtti=false',
    'default_library=static',
  ],
)

fs = import('fs')

add_project_arguments('-march=x86-64-v3', language: 'c')
add_project_arguments('-march=x86-64-v3', language: 'cpp')
add_project_link_arguments('-Wl,--emit-relocs', language: ['c', 'cpp'])

if get_option('force_dwarf4')
  add_global_arguments('-gdwarf-4', language: ['c', 'cpp'])
endif

if get_option('profile')
    bracelet_prof = 'on'
else 
  bracelet_prof = 'off'
endif 

# DEPENDENCIES!

install_bin = find_program('install')

cpp = meson.get_compiler('cpp')

boost_proj = subproject('boost',)
boost = boost_proj.get_variable('boost')

streamvbyte_proj = subproject('streamvbyte')
streamvbyte = streamvbyte_proj.get_variable('streamvbyte')

absl = subproject('abseil-cpp')
absl_synchronization = absl.get_variable('absl_synchronization_dep')
absl_container = absl.get_variable('absl_container_dep')
absl_strings = absl.get_variable('absl_strings_dep')
absl_log = absl.get_variable('absl_log_dep')
absl_debugging = absl.get_variable('absl_debugging_dep')
absl_random = absl.get_variable('absl_random_dep')

libbacktrace = cpp.find_library(
  'backtrace',
  required: false,
  has_headers: 'backtrace.h',
)

catch2_proj = subproject('catch2', default_options: {
  'tests': false, 
  'install': false,
})
catch2 = catch2_proj.get_variable('catch2_with_main_dep')

zstd_proj = subproject('zstd', default_options: {'default_library': 'static'})
zstd = zstd_proj.get_variable('libzstd_internal_dep')

llvm = dependency(
  'llvm',
  version: '>= 20.0',
  method: 'config-tool',
  modules: ['core', 'support', 'transformutils', 'linker'],
)
llvm_with_codegen = dependency(
  'llvm',
  version: '>= 20.0',
  method: 'config-tool',
  modules: ['x86'],
)
lldb_lib = cpp.find_library(
  'lldb',
  dirs: [llvm.get_variable(configtool: 'libdir')],
  required: false,
)
if not lldb_lib.found()
  lldb_lib = cpp.find_library('lldb')
endif
lldb = declare_dependency(
  dependencies: lldb_lib,
  include_directories: [llvm.get_variable(configtool: 'includedir')],
)
heap_layers_proj = subproject('heap-layers')
heap_layers = heap_layers_proj.get_variable('heap_layers')
cli11_proj = subproject(
  'cli11',
  default_options: {'precompiled': true},
)
cli11 = cli11_proj.get_variable('CLI11_dep')
# Technically, we should set up clang as a cross compiler, and then build most of our targets as
# native. But since we don't plan on cross-compiling, it's easier to invoke clang manually.
llvm_bin_dir = llvm.get_variable(configtool: 'bindir')
comp_override_dir = get_option('clang-dir')
if comp_override_dir != ''
  llvm_bin_dir = comp_override_dir
endif
clang = find_program(llvm_bin_dir / 'clang')

# End deps

build_file_table = custom_target(
  'build_file_table.py',
  input: ['build_base/build_file_table.py'],
  output: 'build_file_table.py',
  command: [install_bin, '@INPUT@', '@OUTPUT@'],
  install: true,
  install_mode: 'rwxr-xr-x',
  install_dir: get_option('bindir'),
)

include_dir = include_directories('src')

install_headers('include/bracelet_snapshot/snapshot.hpp', subdir: 'bracelet_snapshot')

subdir('src/Result')
subdir('src/Subprocess')
subdir('src/tempfile')
subdir('src/ObjectParsing')
subdir('src/RuntimeFormat')
subdir('src/Edges')


BraceletReachability = shared_library(
  'bracelet_reachability',
  [
    'src/BraceletReachability/BraceletReachability.cpp',
    BraceletRuntimeStructs['llvm'],
  ],
  include_directories: include_dir,
  cpp_args: get_option('bracelet_llvm_extensions') ? [] : ['-DBRACELET_UPSTREAM_LLVM'],
  dependencies: [llvm, edges_writer, absl_container],
  install: true,
  install_dir: get_option('bindir'),
  install_rpath: llvm.get_variable(configtool: 'libdir'),
  build_rpath: llvm.get_variable(configtool: 'libdir'),
)

trace_runtime = shared_library(
  'bracelet_pointsto_trace_runtime',
  [
    'src/Tracing/Runtime/exports.cpp',
    'src/Tracing/Runtime/trace_runtime.cpp',
    'src/Tracing/Runtime/elf_segments.cpp',
    'src/Tracing/Runtime/vhmalloc.cpp',
    BraceletRuntimeStructs['c'],
  ],
  dependencies: [heap_layers, boost],
  gnu_symbol_visibility: 'hidden',
  link_depends: files('src/Tracing/Runtime/vis.map'),
  link_args: [
    '-Wl,--version-script,' +
    meson.current_source_dir() + '/src/Tracing/Runtime/vis.map',
  ],
  install: true,
  install_dir: get_option('bindir'),
)
dlsym_runtime_bc = custom_target(
  'runtime-dlsym',
  input: ['src/BraceletReachability/dlsym_runtime.c'],
  command: [
    clang,
    '-MMD', '-MF', '@DEPFILE@',
    '-O3', '-c',
    '-I', '@CURRENT_SOURCE_DIR@/src',
    '-I', '@BUILD_ROOT@',
    '-I', '@BUILD_ROOT@/src/RuntimeFormat',
    '-emit-llvm', '-o', '@OUTPUT@', '@INPUT@',
    '-fPIC', '-march=x86-64-v3',
  ],
  output: '@BASENAME@.bc',
  depfile: '@BASENAME@.d',
  install: true,
  install_dir: get_option('bindir'),
  depends: [BraceletRuntimeStructs['c']],
)

wrappers = {}
wrapper_depends = [
  build_file_table,
  BraceletReachability,
  dlsym_runtime_bc,
  trace_runtime,
]

foreach entry: [
  ['cc', 'clang', 'BRACELET_CLANG_OVERRIDE'],
  ['c++', 'clang++', 'BRACELET_CLANGXX_OVERRIDE']
]
  name = entry[0]
  underlying = entry[1]
  override = entry[2]
  extra_flags = ''

  if not get_option('bracelet_debug')
    extra_flags += f'-mllvm=--no-bracelet-include-debug-data'
  endif
  if get_option('force_dwarf4')
    extra_flags += '-gdwarf-4'
  endif
  wrapper_file = configure_file(
    input: 'build_base/compiler_wrapper.sh',
    install_mode: 'rwxr-xr-x',
    install_dir: get_option('bindir'),
    output: f'bracelet-@name@.file.sh',
    configuration: {
      'compiler': llvm_bin_dir / underlying,
      'compiler_override': override,
      'build_file_table': get_option('prefix') / get_option('bindir') / 'build_file_table.py',
      'bracelet_trace': get_option('bracelet_trace'),
      'extra_flags': extra_flags,
      'bracelet_profile': bracelet_prof
    },
  )
  wrapper = custom_target(
    f'bracelet-@name@.sh',
    input: [wrapper_file],
    output: f'bracelet-@name@.sh',
    command: [install_bin, '@INPUT@', '@OUTPUT@'],
    install: true,
    install_mode: 'rwxr-xr-x',
    install_dir: get_option('bindir'),
    depends: wrapper_depends,
  )
  wrappers += {name: wrapper}
endforeach


llvm_prefix = llvm.get_variable(configtool: 'prefix')
conf_data = configuration_data()
conf_data.set('LLVM_INSTALL_PREFIX', llvm_prefix)
conf_data.set('INSTALL_PREFIX', get_option('prefix'))
foreach fl_name: ['arm64-linux-bracelet.cmake.in', 'bracelet-toolchain.cmake.in', 'setup_sbom_env.cmake.in',
    'x64-linux-bracelet.cmake.in', 'x64-linux-braceletnodbg.cmake.in']
  new_name = fl_name.split('.')[0] + '.cmake'
  out_file = configure_file(
    input: 'build_base/cmake' / fl_name,
    output: new_name,
    configuration: conf_data
  )
  install_data(out_file, install_dir: get_option('prefix') / 'toolchain')

endforeach

subdir('src/Tracing')
subdir('src/PointsTo')

braceletEdges = executable(
  'bracelet-edges',
  files([
    'src/bracelet-edges/bracelet-edges.cpp',
  ]),
  dependencies: [
    edges_reader,
    object_parsing,
    cli11,
    absl_container,
    absl_log,
  ],
  install: true,
  install_rpath: llvm.get_variable(configtool: 'libdir'),
  build_rpath: llvm.get_variable(configtool: 'libdir'),
)

if get_option('test')
  test(
    'bracelet metadata embedding test',
    files('src/BraceletReachability/test_metadata.sh'),
    args: [
      meson.current_build_dir() / 'bracelet-cc.sh',
      braceletEdges,
      files('src/PointsTo/test_programs/one.c'),
    ],
    depends: [wrappers['cc'], braceletEdges],
    suite: 'metadata',
  )
endif

executable(
  'bracelet-points-to',
  [
    'src/PointsTo/main.cpp',
  ],
  dependencies: [
    cli11,
    absl_log,
    result,
    boost,
    points_to,
    object_parsing,
    edges_reader,
  ],
  install: true
)

braceletTraceSimplify = executable(
  'bracelet-trace-simplify',
  [
    'src/RuntimeFormat/bracelet-trace-simplify.cpp',
    BraceletRuntimeStructs['c'],
  ],
  dependencies: [
    cli11,
    absl_container,
  ],
  install: true,
)
