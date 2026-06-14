# doctest-based regression test suite, linked against the engine core library.

# doctest: prefer an installed package; otherwise fetch the single header into
# the build tree (no git required). The production image builds with tests OFF.
find_package(doctest QUIET)
if(NOT doctest_FOUND)
    set(DOCTEST_INCLUDE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/doctest-include")
    set(DOCTEST_HEADER "${DOCTEST_INCLUDE_ROOT}/doctest/doctest.h")
    if(NOT EXISTS "${DOCTEST_HEADER}")
        message(STATUS "doctest not found; downloading the single header")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h"
            "${DOCTEST_HEADER}"
            TLS_VERIFY ON
            STATUS DOCTEST_DL_STATUS)
        list(GET DOCTEST_DL_STATUS 0 DOCTEST_DL_CODE)
        if(NOT DOCTEST_DL_CODE EQUAL 0)
            file(REMOVE "${DOCTEST_HEADER}")
            message(FATAL_ERROR "Failed to download doctest header: ${DOCTEST_DL_STATUS}")
        endif()
    endif()
    add_library(doctest::doctest INTERFACE IMPORTED)
    set_target_properties(doctest::doctest PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DOCTEST_INCLUDE_ROOT}")
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(thoth_tests ${TEST_SOURCES})
target_link_libraries(thoth_tests PRIVATE thoth_core doctest::doctest)
target_compile_options(thoth_tests PRIVATE ${THOTH_WARNING_FLAGS})

# the whole doctest binary is one CTest entry (non-zero exit on any failure)
add_test(NAME thoth_tests COMMAND thoth_tests --no-intro --force-colors)
