# Portable fixed-point ASCII integer formatter.
#
# Build integration only, mirroring the driver convention: an INTERFACE
# target so each consumer compiles the source into its own binary with its
# own flags. The same source is used by the ARM firmware target and by the
# host test harness, which is only possible because the unit depends on
# nothing but <stdint.h>.
cmake_minimum_required(VERSION 3.20)

add_library(util_fmt_i32 INTERFACE)

target_sources(util_fmt_i32 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/fmt_i32.c
)

target_include_directories(util_fmt_i32 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
