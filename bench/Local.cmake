# Maul UI's further benchmarks (bench/Local.cmake, family record 0006):
# virtual lists of 100,000 rows, a HUD's frames, and text shaping and
# layout, with the text component.
add_executable(${PROJECT_NAME}_bench_virtual bench_virtual.c)
target_link_libraries(${PROJECT_NAME}_bench_virtual PRIVATE ${PROJECT_NAME})
maul_apply_flags(${PROJECT_NAME}_bench_virtual)

add_executable(${PROJECT_NAME}_bench_hud bench_hud.c)
target_link_libraries(${PROJECT_NAME}_bench_hud PRIVATE ${PROJECT_NAME})
maul_apply_flags(${PROJECT_NAME}_bench_hud)

if(MAUL_UI_TEXT)
    add_executable(${PROJECT_NAME}_bench_text bench_text.c)
    target_link_libraries(${PROJECT_NAME}_bench_text PRIVATE ${PROJECT_NAME})
    maul_apply_flags(${PROJECT_NAME}_bench_text)
    mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/LiberationSans-Regular.ttf
        ${PROJECT_BINARY_DIR}/bench_fonts/liberation_sans.inc s_liberationSans)
    target_include_directories(${PROJECT_NAME}_bench_text PRIVATE ${PROJECT_BINARY_DIR}/bench_fonts)
endif()
