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

# NOTE: CMake is the primary build (and the only one that builds the GUI).
# This Meson build is kept for the CLI + tests so `make` keeps working.
# libdiffy is the shared diff core; tty lives with the CLI now.
src = ['libdiffy/algorithms/myers_greedy.hpp',
       'libdiffy/algorithms/myers_linear.hpp',
       'libdiffy/config/config.hpp',
       'libdiffy/config/config.cc',
       'libdiffy/config/repos.cc',
       'libdiffy/config/gui_config.cc',
       'libdiffy/processing/tokenizer.hpp',
       'libdiffy/processing/tokenizer.cc',
       'libdiffy/processing/diff_hunk.hpp',
       'libdiffy/processing/diff_hunk.cc',
       'libdiffy/processing/diff_hunk_annotate.hpp',
       'libdiffy/processing/diff_hunk_annotate.cc',
       'libdiffy/render/diff_view_model.hpp',
       'libdiffy/render/diff_view_model.cc',
       'libdiffy/render/diff_pipeline.hpp',
       'libdiffy/render/diff_pipeline.cc',
       'libdiffy/output/unified.hpp',
       'libdiffy/output/unified.cc',
       'libdiffy/output/column_view.hpp',
       'libdiffy/output/column_view.cc',
       'libdiffy/util/color.hpp',
       'libdiffy/util/color.cc',
       'libdiffy/util/readlines.hpp',
       'libdiffy/util/readlines.cc',
       'libdiffy/util/utf8decode.hpp',
       'libdiffy/util/utf8decode.cc',
       'libdiffy/util/hash.hpp',
       'libdiffy/util/hash.cc'
]

src_test = [
  'libdiffy/algorithms/algorithm_tests.cc',
  'libdiffy/config/config_tests.cc',
  'libdiffy/output/column_view_tests.cc',
  'libdiffy/output/render_corpus_tests.cc',
  'libdiffy/output/theme_tests.cc',
  'libdiffy/output/unified_tests.cc',
  'libdiffy/processing/diff_hunk_tests.cc',
  'libdiffy/processing/diff_hunk_annotate_tests.cc',
  'libdiffy/processing/tokenizer_tests.cc',
  'libdiffy/render/diff_view_model_tests.cc',
  'libdiffy/util/bipolar_array_tests.cc',
  'libdiffy/util/color_tests.cc',
  'libdiffy/util/readlines_tests.cc',
  'libdiffy/util/utf8decode_tests.cc',
]

# config_parser ships its own tests via config_parser_test_dep (linked below).

test_cases_dir = meson.current_source_dir() / 'tests' / 'test_cases'

hash = run_command('git', 'rev-parse', '--short', 'HEAD', check: true)

executable('diffy',
            ['cli/diffy_main.cc', 'cli/tty.cc'] + src,
            cpp_args :
              [
                '-DDIFFY_VERSION="@0@"'.format(meson.project_version()),
                '-DDIFFY_BUILD_HASH="@0@"'.format(
                    hash.stdout().strip())
              ],
            include_directories: include_directories([
                'libdiffy',
                'cli',
              ]),
            dependencies: [crc32c_dep,
                           GSL_dep,
                           fmt_dep,
                           getopt_dep,
                           platform_folders_dep,
                           config_parser_dep]
          )

test_executable = executable('diffy-test',
              'tests/diffy_test.cc', src, src_test,
              cpp_args : [
                '-DDIFFY_TEST_CASES_DIR="@0@"'.format(test_cases_dir)
              ],
              include_directories: include_directories([
                  'libdiffy',
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
