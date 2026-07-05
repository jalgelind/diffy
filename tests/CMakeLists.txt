# diffy-test — single doctest binary over the whole codebase. Test files live
# next to the code they cover (libdiffy/**/<x>_tests.cc); the config_parser
# subproject ships its own tests too, which we fold in for full coverage.
file(GLOB_RECURSE DIFFY_TEST_SRCS CONFIGURE_DEPENDS
  ${DIFFY_ROOT_DIR}/libdiffy/*_tests.cc)
file(GLOB CONFIG_PARSER_TEST_SRCS CONFIGURE_DEPENDS
  ${DIFFY_ROOT_DIR}/subprojects/config_parser/src/config_parser/*_tests.cc)

add_executable(diffy-test
  diffy_test.cc
  ${DIFFY_TEST_SRCS}
  ${CONFIG_PARSER_TEST_SRCS})

target_link_libraries(diffy-test PRIVATE diffy_core config_parser doctest::doctest)
# Tests include <doctest.h> directly; point at the header's folder.
target_include_directories(diffy-test PRIVATE ${DIFFY_ROOT_DIR}/subprojects/doctest/doctest)
target_compile_definitions(diffy-test PRIVATE
  DIFFY_TEST_CASES_DIR="${DIFFY_ROOT_DIR}/tests/test_cases")

add_test(NAME unit-tests COMMAND diffy-test)
