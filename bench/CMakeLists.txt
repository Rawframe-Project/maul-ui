# The benchmarks: never a CTest gate, because wall time is not
# repeatable. They print their timings (conventions, section 14).
add_executable(${PROJECT_NAME}_bench bench_main.c)
target_link_libraries(${PROJECT_NAME}_bench PRIVATE ${PROJECT_NAME})
maul_apply_flags(${PROJECT_NAME}_bench)

if(MAUL_UI_TEXT)
    add_executable(${PROJECT_NAME}_bench_text bench_text.c)
    target_link_libraries(${PROJECT_NAME}_bench_text PRIVATE ${PROJECT_NAME})
    maul_apply_flags(${PROJECT_NAME}_bench_text)
    mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/LiberationSans-Regular.ttf
        ${PROJECT_BINARY_DIR}/bench_fonts/liberation_sans.inc s_liberationSans)
    target_include_directories(${PROJECT_NAME}_bench_text PRIVATE ${PROJECT_BINARY_DIR}/bench_fonts)
endif()
