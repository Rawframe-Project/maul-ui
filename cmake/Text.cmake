# The text component (record mui-0006): its modules, built into maul-ui
# together with the objects of FreeType, HarfBuzz and Maul Unicode, so
# the library carries what it needs, installs as one archive and links
# with the C linker (HarfBuzz needs no C++ runtime). Only the text
# modules read their headers.

target_sources(maul-ui PRIVATE
    src/font.c
    src/font_store.c
    src/text_service.c
    $<TARGET_OBJECTS:maul-unicode>
    $<TARGET_OBJECTS:maul-unicode-harfbuzz>)
target_include_directories(maul-ui SYSTEM PRIVATE
    $<TARGET_PROPERTY:maul-unicode,INTERFACE_INCLUDE_DIRECTORIES>)

if(MAUL_UI_TEXT_SYSTEM_LIBRARIES)
    target_link_libraries(maul-ui PRIVATE Freetype::Freetype harfbuzz::harfbuzz)
else()
    target_sources(maul-ui PRIVATE
        $<TARGET_OBJECTS:maul_ui_freetype>
        $<TARGET_OBJECTS:maul_ui_harfbuzz>)
    target_include_directories(maul-ui SYSTEM PRIVATE
        $<TARGET_PROPERTY:maul_ui_freetype,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:maul_ui_harfbuzz,INTERFACE_INCLUDE_DIRECTORIES>)
    # HarfBuzz's locks, where the C library does not hold them already.
    if(NOT WIN32 AND NOT EMSCRIPTEN)
        find_package(Threads REQUIRED)
        target_link_libraries(maul-ui PUBLIC ${CMAKE_THREAD_LIBS_INIT})
        string(STRIP "${MAUL_PKG_LIBS_PRIVATE} ${CMAKE_THREAD_LIBS_INIT}" MAUL_PKG_LIBS_PRIVATE)
    endif()
endif()

# A shared library exports Maul UI's functions alone: FreeType marks its
# own for export, which would put them beside a program's own FreeType.
if(MAUL_UI_BUILD_SHARED AND NOT WIN32)
    if(APPLE)
        file(WRITE ${PROJECT_BINARY_DIR}/maul-ui.exported "_mui*\n")
        target_link_options(maul-ui PRIVATE
            "LINKER:-exported_symbols_list,${PROJECT_BINARY_DIR}/maul-ui.exported")
    else()
        file(WRITE ${PROJECT_BINARY_DIR}/maul-ui.map "{\n  global: mui*;\n  local: *;\n};\n")
        target_link_options(maul-ui PRIVATE "LINKER:--version-script=${PROJECT_BINARY_DIR}/maul-ui.map")
    endif()
endif()

# mui_embed_file(<file> <output> <name>) writes a file's bytes as a C
# array named name, for tests that read fonts on every platform, the web
# included.
function(mui_embed_file file output name)
    file(READ ${file} hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    file(WRITE ${output}.new "static const unsigned char ${name}[] = {${bytes}};\n")
    configure_file(${output}.new ${output} COPYONLY)
endfunction()
