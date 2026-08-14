BraceletRuntimeStructs_deps = files(
  'runtime_format.py',
  'templates/llvm.tmpl',
  'templates/lldb.tmpl',
  'templates/test.tmpl',
)
BraceletRuntimeStructs = {}
foreach kind : ['c', 'llvm', 'lldb']
  BraceletRuntimeStructs += {kind: custom_target(
    f'BraceletRuntimeStructs_@kind@.h',
    output: f'BraceletRuntimeStructs_@kind@.h',
    depend_files: BraceletRuntimeStructs_deps,
    command: ['runtime_format.py', '-o', '@OUTPUT@', kind],
  )}
endforeach

BraceletRuntimeStructs_test_c = custom_target(
  f'BraceletRuntimeStructs_test.cpp',
  output: f'BraceletRuntimeStructs_test.cpp',
  depend_files: BraceletRuntimeStructs_deps,
  command: ['runtime_format.py', '-o', '@OUTPUT@', 'test'],
)
BraceletRuntimeStructs_test = executable(
  'BraceletRuntimeStructs_test',
  [
    BraceletRuntimeStructs_test_c,
    BraceletRuntimeStructs['c'],
    BraceletRuntimeStructs['llvm'],
    BraceletRuntimeStructs['lldb'],
  ],
  dependencies: [
    llvm,
    lldb,
  ],
  build_rpath: llvm.get_variable(configtool: 'libdir'),
)
test('Bracelet Runtime Structs Test', BraceletRuntimeStructs_test)

