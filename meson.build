project('diffy', 'cpp', version : '24.07',
  default_options : [
  'cpp_std=c++20',
])

defines = []

if get_option('buildtype') == 'debug'
  defines += [
    '-DDIFFY_DEBUG=1',
  ]
endif

if host_machine.system() == 'windows'
  defines += ['-DDIFFY_PLATFORM_WINDOWS']
else
  # Well... it could be something else.
  defines += ['-DDIFFY_PLATFORM_POSIX']
endif

add_global_arguments(defines, language : 'cpp')

add_global_arguments(['-std=c++20'], language : 'cpp')

cmake = import('cmake')

crc32c_opts = cmake.subproject_options()
crc32c_opts.add_cmake_defines({
  'CRC32C_BUILD_TESTS': false,
  'CRC32C_BUILD_BENCHMARKS': false,
  'CRC32C_USE_GLOG': false,
  'CRC32C_INSTALL': false,
  # crc32c pins cmake_minimum_required below 3.5; CMake >= 4 refuses that without
  # this override. Lets the build work out of the box on modern CMake.
  'CMAKE_POLICY_VERSION_MINIMUM': '3.5'
})

crc32c_proj = cmake.subproject('crc32c', options: crc32c_opts)
crc32c_dep = crc32c_proj.dependency('crc32c')

# Shared override so old CMake subprojects configure on CMake >= 4 (see crc32c above).
cmake_compat_opts = cmake.subproject_options()
cmake_compat_opts.add_cmake_defines({'CMAKE_POLICY_VERSION_MINIMUM': '3.5'})

fmt_proj = cmake.subproject('fmt', options: cmake_compat_opts)
fmt_dep = fmt_proj.dependency('fmt')

GSL_opts = cmake.subproject_options()
GSL_opts.add_cmake_defines({
  'CMAKE_POLICY_VERSION_MINIMUM': '3.5',
  # diffy only needs the headers; skip GSL's test build (pulls in googletest).
  'GSL_TEST': false
})
GSL_proj = cmake.subproject('GSL', options: GSL_opts)
GSL_dep = GSL_proj.dependency('GSL')

platform_folders_proj = cmake.subproject('platform_folders', options: cmake_compat_opts)
platform_folders_dep = platform_folders_proj.dependency('platform_folders')

doctest_proj = subproject('doctest')
doctest_dep = doctest_proj.get_variable('doctest_dep')

getopt_proj = subproject('getopt')
getopt_dep = getopt_proj.get_variable('getopt_dep')

config_parser_proj = subproject('config_parser')
config_parser_dep = config_parser_proj.get_variable('config_parser_dep')
config_parser_test_dep = config_parser_proj.get_variable('config_parser_test_dep')

src = ['src/algorithms/myers_greedy.hpp',
       'src/algorithms/myers_linear.hpp',
       'src/config/config.hpp',
       'src/config/config.cc',
       'src/processing/tokenizer.hpp',
       'src/processing/tokenizer.cc',
       'src/processing/diff_hunk.hpp',
       'src/processing/diff_hunk.cc',
       'src/processing/diff_hunk_annotate.hpp',
       'src/processing/diff_hunk_annotate.cc',
       'src/output/unified.hpp',
       'src/output/unified.cc',
       'src/output/column_view.hpp',
       'src/output/column_view.cc',
       'src/util/color.hpp',
       'src/util/color.cc',
       'src/util/readlines.hpp',
       'src/util/readlines.cc',
       'src/util/tty.hpp',
       'src/util/tty.cc',
       'src/util/utf8decode.hpp',
       'src/util/utf8decode.cc',
       'src/util/hash.hpp',
       'src/util/hash.cc'
]

src_test = [
  'src/algorithms/algorithm_tests.cc',
  'src/config/config_tests.cc',
  'src/output/column_view_tests.cc',
  'src/output/render_corpus_tests.cc',
  'src/output/unified_tests.cc',
  'src/processing/diff_hunk_tests.cc',
  'src/processing/diff_hunk_annotate_tests.cc',
  'src/processing/tokenizer_tests.cc',
  'src/util/bipolar_array_tests.cc',
  'src/util/color_tests.cc',
  'src/util/readlines_tests.cc',
  'src/util/utf8decode_tests.cc',
]

# config_parser ships its own tests via config_parser_test_dep (linked below).

test_cases_dir = meson.current_source_dir() / 'tests' / 'test_cases'

hash = run_command('git', 'rev-parse', '--short', 'HEAD', check: true)

executable('diffy',
            ['src/diffy_main.cc'] + src,
            cpp_args : 
              [
                '-DDIFFY_VERSION="@0@"'.format(meson.project_version()),
                '-DDIFFY_BUILD_HASH="@0@"'.format(
                    hash.stdout().strip())
              ],
            include_directories: include_directories([
                'src',
              ]),
            dependencies: [crc32c_dep,
                           GSL_dep,
                           fmt_dep,
                           getopt_dep,
                           platform_folders_dep,
                           config_parser_dep]
          )

test_executable = executable('diffy-test',
              'src/diffy_test.cc', src, src_test,
              cpp_args : [
                '-DDIFFY_TEST_CASES_DIR="@0@"'.format(test_cases_dir)
              ],
              include_directories: include_directories([
                  'src',
                ]),
              dependencies: [crc32c_dep,
                             GSL_dep,
                             doctest_dep, 
                             fmt_dep,
                             getopt_dep,
                             platform_folders_dep,
                             config_parser_dep,
                             config_parser_test_dep]
            )

test('unit-tests', test_executable)
