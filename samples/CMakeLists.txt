# The samples (record mui-0005): programs that use Maul UI's parts
# together and check their own results, so that each is also a test,
# skipped (77) without an adapter unless MUI_RHI_REQUIRED is set, as
# Maul RHI's samples are. They need the reference renderer, the Maul
# Window glue and the text component.

# A sample's text: Liberation Sans, embedded (THIRD_PARTY.md).
mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/LiberationSans-Regular.ttf
    ${CMAKE_CURRENT_BINARY_DIR}/fonts/sans.inc s_sans)

# mui_add_sample(<name>): samples/<name>.c on the harness.
function(mui_add_sample name)
    set(target sample_${name})
    add_executable(${target} ${name}.c harness.c)
    target_link_libraries(${target} PRIVATE maul-ui maul-ui-rhi maul-ui-window)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                 ${CMAKE_CURRENT_BINARY_DIR}/fonts)
    maul_apply_flags(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /clang:-std=c23)
    endif()
    if(MAUL_UI_BUILD_TESTS)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()

mui_add_sample(tour)
