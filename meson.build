project('diffy', 'cpp', version : '23.11B',
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
  'CRC32C_INSTALL': false
})

crc32c_proj = cmake.subproject('crc32c', options: crc32c_opts)
crc32c_dep = crc32c_proj.dependency('crc32c')

fmt_proj = cmake.subproject('fmt')
fmt_dep = fmt_proj.dependency('fmt')

GSL_proj = cmake.subproject('GSL')
GSL_dep = GSL_proj.dependency('GSL')

platform_folders_proj = cmake.subproject('platform_folders')
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
       'src/processing/context_suggestion.hpp',
       'src/processing/context_suggestion.cc',
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
  'src/processing/context_suggestion_tests.cc',
  'src/processing/tokenizer_tests.cc',
  'src/util/bipolar_array_tests.cc',
  'src/util/readlines_tests.cc',
  'src/util/utf8decode_tests.cc',
]

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
