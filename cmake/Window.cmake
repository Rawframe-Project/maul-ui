# The Maul Window glue (record mui-0007), maul-ui-window: a static library
# beside maul-ui that feeds a window's records to a context, Maul Window
# found installed or fetched at its release tag as Maul RHI is. The core
# and the text component never depend on it.

set(MAUL_UI_WINDOW_VERSION 0.8.0)
find_package(maul-window ${MAUL_UI_WINDOW_VERSION} QUIET)
if(NOT maul-window_FOUND)
    set(MAUL_WINDOW_BUILD_TESTS OFF)
    set(MAUL_WINDOW_BUILD_BENCH OFF)
    set(MAUL_WINDOW_BUILD_SAMPLES OFF)
    set(MAUL_WINDOW_INSTALL OFF)
    # The glue's tests run on Maul Window's headless test backend.
    set(MAUL_WINDOW_TEST_BACKEND ${MAUL_UI_BUILD_TESTS})
    include(FetchContent)
    FetchContent_Declare(maul-window
        GIT_REPOSITORY https://github.com/Rawframe-Project/maul-window.git
        GIT_TAG v${MAUL_UI_WINDOW_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(maul-window)
endif()

add_library(maul-ui-window STATIC window/src/allocator.c window/src/glue.c)
add_library(maul-ui-window::maul-ui-window ALIAS maul-ui-window)
target_include_directories(maul-ui-window PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/window/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(maul-ui-window PUBLIC maul-ui maul-window::maul-window)
maul_apply_flags(maul-ui-window)
