# Bring-up requires Dawn validation even when translated code uses Release.
# Upstream disables validation and robustness in this translation unit with NDEBUG.
set_property(SOURCE "${MKW_AURORA_DIR}/lib/webgpu/gpu.cpp"
    TARGET_DIRECTORY aurora_core APPEND PROPERTY COMPILE_OPTIONS -UNDEBUG)

# Keep upstream's diagnostics, but give Kirby its own process entry point.
set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS main=KirbyLegacyMain)
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/app/main.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/app/frame_log.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/cache.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/native-src/graphics/cache.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/devices/ax.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/native-src/devices/dsp.cpp"
    "${KIRBY_PROJECT_ROOT}/tests/integration/ax_probe.cpp"
    "${KIRBY_PROJECT_ROOT}/generated/functions/func_80684770.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/boot/boot_memory.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/boot/clock.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/devices/interrupts.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/devices/video_si.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/devices/ipl.cpp")
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/services/ios_service.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/native-src/services/ios.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/native-src/threading/scheduler.cpp")
configure_file("${KIRBY_PROJECT_ROOT}/runtime/include/kirby/devices/ipl.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/kirby_ipl.h" COPYONLY)
configure_file("${KIRBY_PROJECT_ROOT}/runtime/include/kirby/devices/interrupts.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/kirby_interrupts.h" COPYONLY)
add_executable(kirby_interrupts_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/interrupts_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/interrupts.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/video_si.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/ipl.cpp")
target_compile_features(kirby_interrupts_test PRIVATE cxx_std_20)
target_compile_options(kirby_interrupts_test PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME kirby_interrupts COMMAND kirby_interrupts_test)
add_executable(kirby_flat_mmio_test "${KIRBY_PROJECT_ROOT}/tests/unit/boot/flat_mmio_test.cpp")
target_include_directories(kirby_flat_mmio_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(kirby_flat_mmio_test PRIVATE cxx_std_20)
target_compile_options(kirby_flat_mmio_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_flat_mmio COMMAND kirby_flat_mmio_test)
add_executable(kirby_video_si_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/video_si_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/video_si.cpp")
target_compile_features(kirby_video_si_test PRIVATE cxx_std_20)
target_compile_options(kirby_video_si_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_video_si COMMAND kirby_video_si_test)
add_executable(kirby_ipl_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/ipl_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/ipl.cpp")
target_compile_features(kirby_ipl_test PRIVATE cxx_std_20)
target_compile_options(kirby_ipl_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_ipl COMMAND kirby_ipl_test)
add_executable(kirby_exi_dma_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/exi_dma_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/interrupts.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/video_si.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/ipl.cpp")
target_compile_features(kirby_exi_dma_test PRIVATE cxx_std_20)
target_compile_options(kirby_exi_dma_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_exi_dma COMMAND kirby_exi_dma_test)
add_executable(kirby_boot_memory_test "${KIRBY_PROJECT_ROOT}/tests/unit/boot/boot_memory_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/boot/boot_memory.cpp")
target_compile_features(kirby_boot_memory_test PRIVATE cxx_std_20)
target_compile_options(kirby_boot_memory_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_boot_memory COMMAND kirby_boot_memory_test)
foreach(target kirby_interrupts_test kirby_exi_dma_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/boot/clock.cpp")
endforeach()
add_executable(kirby_clock_test "${KIRBY_PROJECT_ROOT}/tests/unit/boot/clock_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/boot/clock.cpp")
target_compile_features(kirby_clock_test PRIVATE cxx_std_20)
target_compile_options(kirby_clock_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_clock COMMAND kirby_clock_test)

configure_file("${KIRBY_PROJECT_ROOT}/runtime/include/kirby/devices/dsp.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/kirby_dsp.h" COPYONLY)
foreach(target WiiCompiled kirby_interrupts_test kirby_exi_dma_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/devices/dsp.cpp")
endforeach()
add_executable(kirby_dsp_boot_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/dsp_boot_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/dsp.cpp")
target_compile_features(kirby_dsp_boot_test PRIVATE cxx_std_20)
target_compile_options(kirby_dsp_boot_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_dsp_boot COMMAND kirby_dsp_boot_test)
add_executable(kirby_audio_dma_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/audio_dma_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/audio_dma.cpp")
target_compile_features(kirby_audio_dma_test PRIVATE cxx_std_20)
target_compile_options(kirby_audio_dma_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_audio_dma COMMAND kirby_audio_dma_test)
foreach(target WiiCompiled kirby_dsp_boot_test kirby_interrupts_test kirby_exi_dma_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/devices/audio_dma.cpp")
endforeach()
add_executable(kirby_ios_service_test "${KIRBY_PROJECT_ROOT}/tests/unit/services/ios_service_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/ios_service.cpp")
target_compile_features(kirby_ios_service_test PRIVATE cxx_std_20)
target_compile_options(kirby_ios_service_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_ios_service COMMAND kirby_ios_service_test)
foreach(target WiiCompiled kirby_ios_service_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/services/nand.cpp")
endforeach()
add_executable(kirby_nand_test "${KIRBY_PROJECT_ROOT}/tests/unit/services/nand_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/nand.cpp")
target_compile_features(kirby_nand_test PRIVATE cxx_std_20)
target_compile_options(kirby_nand_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_nand COMMAND kirby_nand_test)
add_executable(kirby_ios_nand_test "${KIRBY_PROJECT_ROOT}/tests/unit/services/ios_nand_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/ios_service.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/nand.cpp")
target_compile_features(kirby_ios_nand_test PRIVATE cxx_std_20)
target_compile_options(kirby_ios_nand_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_ios_nand COMMAND kirby_ios_nand_test)
foreach(target WiiCompiled kirby_ios_service_test kirby_ios_nand_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/services/disc.cpp")
endforeach()
add_executable(kirby_disc_test "${KIRBY_PROJECT_ROOT}/tests/unit/services/disc_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/disc.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/ios_service.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/nand.cpp")
target_compile_features(kirby_disc_test PRIVATE cxx_std_20)
target_compile_options(kirby_disc_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_disc COMMAND kirby_disc_test)
foreach(target WiiCompiled kirby_ios_service_test kirby_ios_nand_test kirby_disc_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/services/es_metadata.cpp")
endforeach()
add_executable(kirby_es_metadata_test "${KIRBY_PROJECT_ROOT}/tests/unit/services/es_metadata_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/es_metadata.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/ios_service.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/disc.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/services/nand.cpp")
target_compile_features(kirby_es_metadata_test PRIVATE cxx_std_20)
target_compile_options(kirby_es_metadata_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_es_metadata COMMAND kirby_es_metadata_test)
add_executable(kirby_ave_gpio_test "${KIRBY_PROJECT_ROOT}/tests/unit/devices/ave_gpio_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/devices/video_si.cpp")
target_compile_features(kirby_ave_gpio_test PRIVATE cxx_std_20)
target_compile_options(kirby_ave_gpio_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_ave_gpio COMMAND kirby_ave_gpio_test)

# Keep every generated gather write on Kirby's CP/PI transport. The upstream
# implementations contain Mario Kart-specific GXData/display-list addresses.
foreach(source gx_fifo.cpp gx_stubs.cpp)
    foreach(suffix Write8 Write16 Write32 WriteFloat WriteBurst)
        set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/hle/gx/${source}" APPEND PROPERTY
            COMPILE_DEFINITIONS "GX_HLE_FIFO_${suffix}=KirbyUnusedUpstreamFifo_${suffix}")
    endforeach()
endforeach()
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_bridge.cpp")
foreach(target WiiCompiled kirby_interrupts_test kirby_exi_dma_test)
    target_sources(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_fifo.cpp")
endforeach()
add_executable(kirby_gx_fifo_test "${KIRBY_PROJECT_ROOT}/tests/unit/graphics/gx_fifo_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_fifo.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_bridge.cpp")
target_compile_features(kirby_gx_fifo_test PRIVATE cxx_std_20)
target_compile_options(kirby_gx_fifo_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_gx_fifo COMMAND kirby_gx_fifo_test)

configure_file("${KIRBY_PROJECT_ROOT}/runtime/include/kirby/graphics/gx_fifo.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/kirby_gx_fifo.h" COPYONLY)

add_library(kirby_gx_renderer OBJECT "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_renderer.cpp")
target_sources(kirby_gx_renderer PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_xfb.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/xfb.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_draw.cpp")
target_include_directories(kirby_gx_renderer PRIVATE "${MKW_AURORA_DIR}/lib")
target_link_libraries(kirby_gx_renderer PRIVATE aurora::gx absl::flat_hash_map Tracy::TracyClient)
target_compile_features(kirby_gx_renderer PRIVATE cxx_std_20)
target_compile_options(kirby_gx_renderer PRIVATE -g)
target_compile_definitions(kirby_gx_renderer PRIVATE TARGET_PC SDL_MAIN_HANDLED)
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_stream.cpp"
    $<TARGET_OBJECTS:kirby_gx_renderer>)
add_executable(kirby_gx_stream_test "${KIRBY_PROJECT_ROOT}/tests/unit/graphics/gx_stream_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_stream.cpp")
target_compile_features(kirby_gx_stream_test PRIVATE cxx_std_20)
target_compile_options(kirby_gx_stream_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_gx_stream COMMAND kirby_gx_stream_test)
add_executable(kirby_gx_renderer_test "${KIRBY_PROJECT_ROOT}/tests/unit/graphics/gx_renderer_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_fifo.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/gx_stream.cpp")
target_link_libraries(kirby_gx_renderer_test PRIVATE kirby_gx_renderer
    "$<LINK_GROUP:RESCAN,aurora::gx,aurora::vi,aurora::core>" absl::flat_hash_map Tracy::TracyClient)
target_include_directories(kirby_gx_renderer_test PRIVATE "${MKW_AURORA_DIR}/lib")
target_compile_features(kirby_gx_renderer_test PRIVATE cxx_std_20)
add_test(NAME kirby_gx_renderer COMMAND kirby_gx_renderer_test)
add_executable(kirby_xfb_test "${KIRBY_PROJECT_ROOT}/tests/unit/graphics/xfb_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/xfb.cpp")
target_compile_features(kirby_xfb_test PRIVATE cxx_std_20)
target_compile_options(kirby_xfb_test PRIVATE -Wall -Wextra -Werror)
add_test(NAME kirby_xfb COMMAND kirby_xfb_test)

# WPAD uses Aurora's native input backend; guest reports/callbacks stay in Kirby's bridge.
target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/native-src/input/wpad.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/input/controller_mapping.cpp")
target_link_libraries(WiiCompiled PRIVATE aurora::wpad absl::flat_hash_map)

target_sources(WiiCompiled PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/src/threading/continuations.cpp"
    "${KIRBY_PROJECT_ROOT}/tests/integration/thread_probe.cpp"
    "${KIRBY_PROJECT_ROOT}/tests/integration/controller_probe.cpp")
target_include_directories(WiiCompiled PRIVATE "${MKW_AURORA_DIR}/lib")

add_executable(kirby_cache_test "${KIRBY_PROJECT_ROOT}/tests/unit/graphics/cache_test.cpp"
    "${KIRBY_PROJECT_ROOT}/runtime/src/graphics/cache.cpp")
foreach(address 80663A80 80663AB0 80663AE0 80663B10 80663B40)
    target_sources(kirby_cache_test PRIVATE "${KIRBY_PROJECT_ROOT}/generated/functions/func_${address}.cpp")
endforeach()
target_include_directories(kirby_cache_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(kirby_cache_test PRIVATE cxx_std_20)
add_test(NAME kirby_cache COMMAND kirby_cache_test)

add_library(kirby_settings STATIC "${KIRBY_PROJECT_ROOT}/runtime/src/app/settings.cpp")
target_include_directories(kirby_settings PUBLIC "${KIRBY_PROJECT_ROOT}/runtime/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(kirby_settings PRIVATE mkw::toml11)
target_compile_features(kirby_settings PUBLIC cxx_std_20)
target_compile_options(kirby_settings PRIVATE -Wall -Wextra -Werror)
target_link_libraries(WiiCompiled PRIVATE kirby_settings)
add_executable(kirby_settings_test "${KIRBY_PROJECT_ROOT}/tests/unit/settings_test.cpp")
target_link_libraries(kirby_settings_test PRIVATE kirby_settings)
add_test(NAME kirby_settings COMMAND kirby_settings_test)

# Only owned targets and the patched upstream runtime need these headers.
# Generated translation targets keep their existing include paths.
get_property(kirby_build_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
set(kirby_test_targets)
foreach(target IN LISTS kirby_build_targets)
    if(target MATCHES "^kirby_.*_test$")
        list(APPEND kirby_test_targets ${target})
    endif()
    if(target MATCHES "^kirby_" OR target STREQUAL "WiiCompiled" OR target STREQUAL "mkw_runtime_common")
        target_include_directories(${target} PRIVATE "${KIRBY_PROJECT_ROOT}/runtime/include")
    endif()
endforeach()

add_custom_target(kirby_tests DEPENDS ${kirby_test_targets})

add_executable(kirby-launcher "${KIRBY_PROJECT_ROOT}/launcher/main.cpp")
target_sources(kirby-launcher PRIVATE
    "${KIRBY_PROJECT_ROOT}/launcher/pages/layout.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/play.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/install.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/display.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/audio.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/controls.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/saves.cpp"
    "${KIRBY_PROJECT_ROOT}/launcher/pages/diagnostics.cpp")
target_include_directories(kirby-launcher PRIVATE "${KIRBY_PROJECT_ROOT}/launcher")
target_compile_features(kirby-launcher PRIVATE cxx_std_20)
target_compile_definitions(kirby-launcher PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(kirby-launcher PRIVATE imgui imgui_backends SDL3::SDL3-static kirby_settings)
set_target_properties(kirby-launcher PROPERTIES BUILD_RPATH "\$ORIGIN/../../local/host-libs")
