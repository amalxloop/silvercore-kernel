cmake_minimum_required(VERSION 3.20)
project(SilverCore VERSION 0.1.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(WASM        "Build WebAssembly target via Emscripten"  OFF)
option(SC_ASAN     "Enable AddressSanitizer"                  OFF)
option(SC_UBSAN    "Enable UndefinedBehaviourSanitizer"       OFF)
option(SC_TESTS    "Build test suite"                         ON)
option(SC_SHARED   "Build shared library (libsc_kernel.so)"   ON)

# Graphics backend selection
set(SC_GFX_BACKEND "SOFTWARE" CACHE STRING "Graphics backend: SOFTWARE, VULKAN")
set_property(CACHE SC_GFX_BACKEND PROPERTY STRINGS SOFTWARE VULKAN)

# ---------------------------------------------------------------------------
# Global compile settings
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # for ucontext_t etc.

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Backend-specific setup
if(SC_GFX_BACKEND STREQUAL "VULKAN")
    find_package(Vulkan REQUIRED)
    set(SC_GFX_BACKEND_DEF "SC_GFX_BACKEND_VULKAN")
    set(SC_BACKEND_VULKAN_IMPL "SC_BACKEND_VULKAN_IMPLEMENTATION")
    set(SC_GFX_LIBS Vulkan::Vulkan)
    # Platform surface extensions (XCB, Xlib, Wayland)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XCB xcb)
        pkg_check_modules(WAYLAND wayland-client)
    endif()
    if(XCB_FOUND)
        list(APPEND SC_GFX_LIBS ${XCB_LIBRARIES})
        list(APPEND SC_GFX_INCLUDES ${XCB_INCLUDE_DIRS})
        add_compile_definitions(VK_USE_PLATFORM_XCB_KHR)
        message(STATUS "Vulkan: XCB surface support enabled")
    endif()
    if(WAYLAND_FOUND)
        list(APPEND SC_GFX_LIBS ${WAYLAND_LIBRARIES})
        list(APPEND SC_GFX_INCLUDES ${WAYLAND_INCLUDE_DIRS})
        add_compile_definitions(VK_USE_PLATFORM_WAYLAND_KHR)
        message(STATUS "Vulkan: Wayland surface support enabled")
    endif()
    if(NOT XCB_FOUND AND NOT WAYLAND_FOUND)
        message(WARNING "No window system found; windowed Vulkan mode disabled, headless only")
    endif()
else()
    set(SC_GFX_BACKEND_DEF "SC_GFX_BACKEND_SOFTWARE")
    set(SC_GFX_LIBS "")
    set(SC_GFX_INCLUDES "")
endif()

# Optimisation / warning flags
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-ffast-math>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
)
if(NOT WASM)
    add_compile_options($<$<CONFIG:Release>:-march=native>)
endif()

if(SC_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(SC_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/tools)

# ---------------------------------------------------------------------------
# Core implementation TU
# ---------------------------------------------------------------------------
# The kernel is a "unity build": each header with _IMPLEMENTATION defined
# is #included from sc_kernel.c.  This improves inlining across subsystems.
file(WRITE ${CMAKE_BINARY_DIR}/sc_kernel.c
    "/* Auto-generated unity build TU */\n"
    "#define SC_GFX_IMPLEMENTATION\n"
    "#define SC_LAYOUT_IMPLEMENTATION\n"
    "#define SC_WIDGET_IMPLEMENTATION\n"
    "#define SC_RUNTIME_IMPLEMENTATION\n"
    "#define SC_FONT_IMPLEMENTATION\n"
    "#define ${SC_GFX_BACKEND_DEF}\n"
    "#include \"sc_types.h\"\n"
    "#include \"sc_math.h\"\n"
    "#include \"sc_arena.h\"\n"
    "#include \"sc_layout.h\"\n"
    "#include \"sc_gfx.h\"\n"
    "#include \"sc_font.h\"\n"
    "#include \"sc_widget.h\"\n"
    "#include \"sc_runtime.h\"\n"
    "#ifdef SC_GFX_BACKEND_VULKAN\n"
    "#define SC_BACKEND_VULKAN_IMPLEMENTATION\n"
    "#include \"sc_backend_vulkan.h\"\n"
    "#endif\n"
)

set(KERNEL_SOURCES ${CMAKE_BINARY_DIR}/sc_kernel.c)

# ---------------------------------------------------------------------------
# Static library
add_library(sc_kernel_static STATIC ${KERNEL_SOURCES})
target_include_directories(sc_kernel_static PUBLIC ${CMAKE_SOURCE_DIR}/include ${SC_GFX_INCLUDES})
target_compile_definitions(sc_kernel_static PUBLIC ${SC_GFX_BACKEND_DEF})
target_link_libraries(sc_kernel_static PRIVATE ${SC_GFX_LIBS})
set_target_properties(sc_kernel_static PROPERTIES OUTPUT_NAME "sc_kernel")

# ---------------------------------------------------------------------------
# Shared library (for Python ctypes bindings)
if(SC_SHARED)
    add_library(sc_kernel SHARED ${KERNEL_SOURCES})
    target_include_directories(sc_kernel PUBLIC ${CMAKE_SOURCE_DIR}/include ${SC_GFX_INCLUDES})
    target_compile_definitions(sc_kernel PUBLIC ${SC_GFX_BACKEND_DEF})
    target_link_libraries(sc_kernel PRIVATE m ${SC_GFX_LIBS})
    # Export all public symbols (clang / gcc default behaviour for shared libs)
    set_target_properties(sc_kernel PROPERTIES
        VERSION   ${PROJECT_VERSION}
        SOVERSION 0
    )
endif()

# ---------------------------------------------------------------------------
# Stock Dashboard PoC executable
# ---------------------------------------------------------------------------
if(NOT WASM)
add_executable(stock_dashboard
    ${CMAKE_SOURCE_DIR}/apps/stock_dashboard/stock_dashboard.c
)
# stock_dashboard.c has its own _IMPLEMENTATION defines at the top
target_include_directories(stock_dashboard PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(stock_dashboard PRIVATE m ${SC_GFX_LIBS})
target_compile_options(stock_dashboard PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -ffast-math>
)
endif()

# ---------------------------------------------------------------------------
# Test suite
# ---------------------------------------------------------------------------
if(SC_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# WASM target (included only when building with Emscripten)
# ---------------------------------------------------------------------------
if(WASM OR EMSCRIPTEN)
    include(${CMAKE_SOURCE_DIR}/tools/wasm/CMakeLists.wasm.cmake)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS sc_kernel_static ARCHIVE DESTINATION lib)
if(SC_SHARED)
    install(TARGETS sc_kernel LIBRARY DESTINATION lib)
endif()
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/ DESTINATION include/silvercore
    FILES_MATCHING PATTERN "*.h"
)
if(TARGET stock_dashboard)
    install(TARGETS stock_dashboard RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== SilverCore build configuration ===")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler    : ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Shared lib  : ${SC_SHARED}")
message(STATUS "  WASM        : ${WASM}")
message(STATUS "  Tests       : ${SC_TESTS}")
message(STATUS "  ASan        : ${SC_ASAN}")
message(STATUS "  UBSan       : ${SC_UBSAN}")
message(STATUS "")
