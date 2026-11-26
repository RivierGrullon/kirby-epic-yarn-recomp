# Build the production renderer and its synthetic checks without a game DOL.
cmake_minimum_required(VERSION 3.25)
project(KirbyRendererChecks LANGUAGES C CXX)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
    message(FATAL_ERROR "The pinned renderer requires Clang, matching the production runtime")
endif()
get_filename_component(KIRBY_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT KIRBY_AURORA_DIR OR NOT EXISTS "${KIRBY_AURORA_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set KIRBY_AURORA_DIR to the pinned Aurora snapshot with Kirby's patches applied")
endif()
include("${KIRBY_PROJECT_ROOT}/cmake/apple_threads.cmake")
set(AURORA_DAWN_PROVIDER package CACHE STRING "" FORCE)
set(AURORA_SDL3_PROVIDER vendor CACHE STRING "" FORCE)
set(AURORA_ENABLE_DVD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(AURORA_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
# Kirby supplies its Wii SDK services; match the production runtime's GX/VI kit.
set(AURORA_TARGET_RVL OFF CACHE BOOL "" FORCE)
add_subdirectory("${KIRBY_AURORA_DIR}" aurora EXCLUDE_FROM_ALL)
# Match the game's validation policy, including optimized builds.
set_property(SOURCE "${KIRBY_AURORA_DIR}/lib/webgpu/gpu.cpp"
    TARGET_DIRECTORY aurora_core APPEND PROPERTY COMPILE_OPTIONS -UNDEBUG)
add_executable(kirby_gx_renderer_test
    "${KIRBY_PROJECT_ROOT}/tests/unit/graphics/gx_renderer_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_renderer.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_xfb.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_draw.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/xfb.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_fifo.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_stream.cpp")
target_include_directories(kirby_gx_renderer_test PRIVATE
    "${KIRBY_PROJECT_ROOT}/runtime/include" "${KIRBY_AURORA_DIR}/lib")
target_compile_definitions(kirby_gx_renderer_test PRIVATE TARGET_PC SDL_MAIN_HANDLED)
if(APPLE)
    target_link_libraries(kirby_gx_renderer_test PRIVATE aurora::gx aurora::vi aurora::core)
else()
    target_link_libraries(kirby_gx_renderer_test PRIVATE "$<LINK_GROUP:RESCAN,aurora::gx,aurora::vi,aurora::core>")
endif()
target_link_libraries(kirby_gx_renderer_test PRIVATE absl::flat_hash_map Tracy::TracyClient)
enable_testing()
add_test(NAME renderer_cpu_contracts COMMAND kirby_gx_renderer_test)
set_tests_properties(renderer_cpu_contracts PROPERTIES TIMEOUT 30)
# GPU modes are run explicitly on a Mac with a Metal device. Hosted CI may
# provide no accelerated GPU; CPU-only success does not establish Metal support.
