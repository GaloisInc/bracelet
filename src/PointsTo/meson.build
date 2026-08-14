points_to_template_names = ['c.txt', 'prelude.txt', 'conservative.txt', 'define_globals.txt', 'svf.Makefile']
points_to_template_srcs = []

foreach name : points_to_template_names
  points_to_template_srcs += custom_target(
    f'template_@name@.inc',
    output: f'template_@name@.inc',
    input: f'templates/@name@',
    command: ['templates/gen_template.py', '--input', '@INPUT@', '--output', '@OUTPUT@']
  )
endforeach

points_to_lib = static_library(
  'bracelet-points-to',
  points_to_template_srcs + [
    'PointsTo.cpp',
    BraceletRuntimeStructs['lldb'],
  ],
  include_directories: include_dir,
  dependencies: [
    boost,
    result,
    object_parsing,
    edges_reader,
    absl_container,
    subprocess,
    tempfile,
    lldb,
  ],
)

points_to = declare_dependency(
  link_with: [points_to_lib],
  include_directories: include_dir,
)

ctests = [
  'one.c',
  'two.c',
  'three.c',
  'memalign.c',
  'pass_arg.c',
  'pthread_create.c',
  'test_transparent_override.c',
  'vtable.c',
  'vtable_3.c',
  'vtable_dyn.c',
  'vtable_setter.c',
  'dlsym.c',
  'dlsym-null.c',
  'varargs_data.c',
  'varargs_simple.c',
  'varargs.c',
  'varargs_copy.c'
]
cpptests = [
  'five.cpp',
  'five_between_functions.cpp',
  'nested_indirect_calls.cpp',
  'map.cpp'
]

foreach trace_level: ['off','indirect_callees','full']
  foreach ctest : ctests
    test(
      f'@ctest@ (@trace_level@)',
      files('test.sh'),
      args: [ctest],
      env: {
        'BRACELET_TRACE': trace_level,
	'COMPILER': 'bracelet-cc.sh',
      },
      timeout: 30,
    )
  endforeach

  foreach cpptest : cpptests
    test(
      f'@cpptest@ (@trace_level@)',
      files('test.sh'),
      args: [cpptest],
      env: {
        'BRACELET_TRACE': trace_level,
	'COMPILER': 'bracelet-c++.sh',
      },
      timeout: 90,
    )
  endforeach
endforeach
