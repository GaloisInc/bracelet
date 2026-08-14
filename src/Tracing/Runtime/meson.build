build_tests = get_option('test')
if build_tests

foreach opt_level: ['0', '1', '2', '3']
  foreach tuple: [
    ['', []],
    ['.ll', ['-S', '-emit-llvm']],
  ]
    ext = tuple[0]
    args = tuple[1]
    tgt = custom_target(
      f'vhmalloc_trace_testsO@opt_level@_@ext@',
      input: ['vhmalloc_trace_tests.cpp'],
      command: [
        wrappers['c++'],
        '-MMD', '-MF', '@DEPFILE@',
        f'-O@opt_level@', '-I', '@CURRENT_SOURCE_DIR@/src',
        '-I', '@BUILD_ROOT@',
	'-Wl,-rpath=@BUILD_ROOT@',
	'-L@BUILD_ROOT@',
	'-lbracelet_pointsto_trace_runtime',
        '-o', '@OUTPUT@', '@INPUT@',
        '-fPIC', '-march=x86-64-v3',
      ] + args,
      env: {
        'BRACELET_TRACE': 'full',
      },
      output: '@BASENAME@_O' + opt_level + ext,
      depfile: '@BASENAME@_O' + opt_level + '.d',
      depends: wrapper_depends,
      build_by_default: true,
    )
    if ext == ''
      test(
        f'vhmalloc trace tests -O@opt_level@',
        tgt,
        env: {
          'BRACELET_TRACE_DIR': '.',
        },
      )
    endif
  endforeach
endforeach



  test(
    'vhmalloc setTag test',
    executable(
      'vhmalloc_test_set_tag',
      'vhmalloc_test_set_tag.cpp',
      dependencies: [declare_dependency(link_with: [trace_runtime])],
    ),
  )


  test(
    'trace_runtime_check_exported_symbols',
    files('check_exported_symbols.py'),
    args: [trace_runtime],
    depends: [trace_runtime],
  )

  test(
    'trace_runtime_hash_set_test',
    executable(
      'trace_runtime_hash_set_test_exe',
      [
        'test_edge_hash_set.cpp',
        BraceletRuntimeStructs['c'],
      ],
      dependencies: [boost, absl_container],
    ),
    timeout: 300,
  )

  test(
    'vhmalloc_original_tests',
      executable(
      'vhmalloc_original_tests',
      'vhmalloc_original_test.c',
      dependencies: [declare_dependency(link_with: [trace_runtime])],
    ),
    timeout: 120,
  )


  test(
    'elf_segments test',
    executable(
      'elf_segments_test',
      'elf_segments_test/main.cpp',
      dependencies: [declare_dependency(link_with: [trace_runtime])],
    ),
    args: [shared_library(
      'elf_segments_test_lib',
      'elf_segments_test/lib.cpp',
    )],
    timeout: 120,
  )
endif
