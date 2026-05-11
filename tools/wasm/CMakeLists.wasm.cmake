# tools/wasm/CMakeLists.wasm.cmake
#
# Emscripten / WebAssembly build helper
# =====================================
# Include from the top-level CMakeLists.txt when WASM=ON:
#
#   cmake -DWASM=ON \
#         -DCMAKE_TOOLCHAIN_FILE=/path/to/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake \
#         -G Ninja ..
#
# Output: silvercore.wasm + silvercore.js (ES module)
#         The JS module is suitable for import in a browser or Node.js.
#
# Deployment
#   Host the .wasm + .js alongside an HTML shell page.
#   Use Emscripten's --shell-file flag to customise the HTML wrapper.

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This file requires an Emscripten toolchain. "
                        "Pass -DCMAKE_TOOLCHAIN_FILE=<emsdk>/Emscripten.cmake")
endif()

# ---------------------------------------------------------------------------
# Link flags
# ---------------------------------------------------------------------------
set(WASM_LINK_FLAGS
    "-s WASM=1"
    "-s ALLOW_MEMORY_GROWTH=1"
    "-s MAXIMUM_MEMORY=256MB"
    "-s INITIAL_MEMORY=32MB"
    "-s MODULARIZE=1"
    "-s EXPORT_ES6=1"
    "-s EXPORT_NAME=SilverCore"
    "-s ENVIRONMENT=web,worker"
    "-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue']"
    # Exported C functions (must match sc_gfx.h / sc_widget.h / sc_runtime.h API)
    "-s EXPORTED_FUNCTIONS=["
        "'_sc_gfx_init',"
        "'_sc_gfx_shutdown',"
        "'_sc_gfx_begin_frame',"
        "'_sc_gfx_end_frame',"
        "'_sc_gfx_resize',"
        "'_sc_gfx_draw_rect',"
        "'_sc_gfx_draw_line',"
        "'_sc_gfx_draw_sprite',"
        "'_sc_scene_init',"
        "'_sc_scene_update',"
        "'_sc_scene_render',"
        "'_sc_scene_dispatch_event',"
        "'_sc_widget_rect',"
        "'_sc_widget_text',"
        "'_sc_widget_canvas',"
        "'_sc_widget_set_text',"
        "'_sc_widget_set_color',"
        "'_sc_anim_push',"
        "'_sc_anim_stop',"
        "'_sc_loop_init',"
        "'_sc_loop_tick',"
        "'_sc_task_post',"
        "'_sc_timer_set',"
        "'_sc_timer_repeat',"
        "'_sc_timer_clear',"
        "'_sc_clock_ns',"
        "'_malloc','_free'"
    "]"
    # Asyncify: allows C fibers to suspend and resume via JS Promises
    "-s ASYNCIFY=1"
    "-s ASYNCIFY_IMPORTS=['js_await_frame']"
    # Optimisation
    "-O3"
    "--closure 1"
)

string(JOIN " " WASM_LINK_FLAGS_STR ${WASM_LINK_FLAGS})

# ---------------------------------------------------------------------------
# WASM target
# ---------------------------------------------------------------------------
add_executable(silvercore_wasm
    ${CMAKE_BINARY_DIR}/sc_kernel.c          # auto-generated unity build TU
    ${CMAKE_SOURCE_DIR}/apps/stock_dashboard/stock_dashboard.c
)

target_compile_definitions(silvercore_wasm PRIVATE
    SC_GFX_IMPLEMENTATION
    SC_LAYOUT_IMPLEMENTATION
    SC_WIDGET_IMPLEMENTATION
    SC_RUNTIME_IMPLEMENTATION
    SC_FONT_IMPLEMENTATION
    SC_GFX_BACKEND_SOFTWARE   # WebGPU path TBD
    SC_PLATFORM_WASM
)

target_include_directories(silvercore_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tools
)

# stock_dashboard.c has its own _IMPLEMENTATION defines; suppress them
# when compiling as part of the unity build to avoid duplicate symbols.
set_source_files_properties(${CMAKE_SOURCE_DIR}/apps/stock_dashboard/stock_dashboard.c
    PROPERTIES COMPILE_DEFINITIONS "SC_NO_IMPLEMENTATION"
)

set_target_properties(silvercore_wasm PROPERTIES
    OUTPUT_NAME "silvercore"
    SUFFIX ".js"
)
target_link_options(silvercore_wasm PRIVATE ${WASM_LINK_FLAGS})

target_compile_options(silvercore_wasm PRIVATE
    -O3
    -ffast-math
    -fno-exceptions
    -fno-rtti
    --no-entry
)
