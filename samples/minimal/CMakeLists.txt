# A separate project that knows nothing about the maul-ui source tree and
# consumes only the installed package. CI installs the library, shared
# and static, and builds this; if it builds, links and runs, the package
# works. pkg-config builds the same file:
#   cmake -S samples/minimal -B build-minimal -DCMAKE_PREFIX_PATH=<prefix>
#   cc main.c $(pkg-config --cflags --libs maul-ui)
cmake_minimum_required(VERSION 3.25)
project(maul-ui_minimal LANGUAGES C)

find_package(maul-ui REQUIRED CONFIG)
add_executable(minimal main.c)
target_link_libraries(minimal PRIVATE maul-ui::maul-ui)
set_target_properties(minimal PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON)
