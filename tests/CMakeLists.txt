cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

# Shared compile flags for tests
set(TEST_DEFS
    SC_GFX_IMPLEMENTATION
    SC_LAYOUT_IMPLEMENTATION
    SC_WIDGET_IMPLEMENTATION
    SC_RUNTIME_IMPLEMENTATION
    ${SC_GFX_BACKEND_DEF}
)

# Helper macro
macro(sc_test name src)
    add_executable(test_${name} ${src})
    target_include_directories(test_${name} PRIVATE ${CMAKE_SOURCE_DIR}/include ${SC_GFX_INCLUDES})
    target_compile_definitions(test_${name} PRIVATE ${TEST_DEFS})
    target_link_libraries(test_${name} PRIVATE m ${SC_GFX_LIBS})
    add_test(NAME ${name} COMMAND test_${name})
endmacro()

sc_test(input   ${CMAKE_CURRENT_SOURCE_DIR}/test_input.c)
sc_test(arena   ${CMAKE_CURRENT_SOURCE_DIR}/test_arena.c)
sc_test(math    ${CMAKE_CURRENT_SOURCE_DIR}/test_math.c)
sc_test(layout  ${CMAKE_CURRENT_SOURCE_DIR}/test_layout.c)
sc_test(gfx     ${CMAKE_CURRENT_SOURCE_DIR}/test_gfx.c)
sc_test(runtime ${CMAKE_CURRENT_SOURCE_DIR}/test_runtime.c)
sc_test(font    ${CMAKE_CURRENT_SOURCE_DIR}/test_font.c)
sc_test(bench_gfx ${CMAKE_CURRENT_SOURCE_DIR}/bench_gfx.c)
sc_test(image   ${CMAKE_CURRENT_SOURCE_DIR}/test_image.c)
sc_test(fuzz_gfx ${CMAKE_CURRENT_SOURCE_DIR}/fuzz_gfx.c)
