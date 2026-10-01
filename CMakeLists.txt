cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-ui)
set(MAUL_API_PREFIX mui)
set(MAUL_MACRO_PREFIX MUI)
set(MAUL_TITLE "Maul UI")
set(MAUL_PREFIX MAUL_UI)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-ui/base.h MUI MAUL_VERSION)
project(maul-ui
    VERSION ${MAUL_VERSION}
    DESCRIPTION "An engine-agnostic retained UI core: layout, style, text, interaction, accessibility and a draw-command list"
    LANGUAGES C)

option(MAUL_UI_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_UI_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_UI_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_UI_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_UI_COVERAGE "Instrument for gcov coverage" OFF)

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_UI_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-ui ${MAUL_LIBRARY_KIND}
    src/allocator.c
    src/base.c
    src/context.c
    src/flex.c
    src/flex_resolve.c
    src/layout.c
    src/node.c
    src/tree.c)
maul_configure_library(maul-ui)
# fmaxf, fminf and fabsf, the exactly rounded functions the solver takes
# from libm (mui-0001).
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(maul-ui PUBLIC m)
endif()

if(MAUL_UI_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(context)
    maul_add_test(layout)
    maul_add_test(layout_fixtures)
    maul_add_test(node)
    maul_add_test(tree WHITEBOX)
    # winnls.h defines MUI_ macros; this checks that none clash (family
    # record 0007).
    if(WIN32)
        maul_add_test(windows_headers)
    endif()
    maul_add_header_tests()
endif()

if(MAUL_UI_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_UI_INSTALL)
    include(cmake/Install.cmake)
endif()
