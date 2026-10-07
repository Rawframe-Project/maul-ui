# The samples (record mui-0005): programs that use Maul UI's parts
# together and check their own results, so that each is also a test,
# skipped (77) without an adapter unless MUI_RHI_REQUIRED is set, as
# Maul RHI's samples are. They need the reference renderer, the Maul
# Window glue and the text component.

# A sample's text: Liberation Sans, embedded (THIRD_PARTY.md).
mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/LiberationSans-Regular.ttf
    ${CMAKE_CURRENT_BINARY_DIR}/fonts/sans.inc s_sans)

# mui_add_sample(<name>): samples/<name>.c on the application frame and
# the harness.
function(mui_add_sample name)
    set(target sample_${name})
    add_executable(${target} ${name}.c app.c harness.c)
    target_link_libraries(${target} PRIVATE maul-ui maul-ui-rhi maul-ui-window)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                 ${CMAKE_CURRENT_BINARY_DIR}/fonts)
    maul_apply_flags(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /clang:-std=c23)
    endif()
    if(EMSCRIPTEN)
        # Waits sleep through JSPI; the runner sets MUI_RHI_REQUIRED in ENV.
        target_link_options(${target} PRIVATE -sJSPI -sEXIT_RUNTIME -sALLOW_MEMORY_GROWTH
                                              -sEXPORTED_RUNTIME_METHODS=ENV)
    endif()
    if(MAUL_UI_BUILD_TESTS)
        if(EMSCRIPTEN)
            # In headless Chrome's WebGPU, through the web runner.
            add_test(NAME ${target} COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                                            $<TARGET_FILE:${target}> --headless)
        else()
            add_test(NAME ${target} COMMAND ${target} --headless)
        endif()
        set_tests_properties(${target} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()

mui_add_sample(tour)
mui_add_sample(controls)
mui_add_sample(ranges)
mui_add_sample(text)
mui_add_sample(pickers)
mui_add_sample(tabs_menus)
mui_add_sample(dialogs)
mui_add_sample(collections)
