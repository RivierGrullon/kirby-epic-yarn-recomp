# SPDX-License-Identifier: GPL-3.0-only
option(KIRBY_PROFILE_DAWN_CALLS "Instrument Linux WebGPU calls (diagnostic builds only)" OFF)
set(kirby_profile_compiler_options "")
if(KIRBY_PROFILE_DAWN_CALLS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "KIRBY_PROFILE_DAWN_CALLS requires Linux and linker --wrap support")
    endif()
    foreach(product WiiCompiled kirby_gx_renderer_test)
        target_sources(${product} PRIVATE "${KIRBY_PROJECT_ROOT}/tools/profiling/dawn_calls.cpp")
        foreach(entry wgpuDeviceCreateTexture wgpuTextureCreateView wgpuDeviceCreateBindGroup
                wgpuDeviceCreateSampler wgpuDeviceCreateBuffer wgpuDeviceCreateShaderModule
                wgpuDeviceCreateRenderPipeline wgpuCommandEncoderFinish wgpuQueueSubmit
                wgpuQueueWriteBuffer wgpuQueueWriteTexture wgpuSurfaceGetCurrentTexture
                wgpuSurfacePresent aurora_wait_for_frame_worker)
            target_link_options(${product} PRIVATE "LINKER:--wrap=${entry}")
        endforeach()
    endforeach()
    set(kirby_profile_compiler_options " KIRBY_PROFILE_DAWN_CALLS=ON")
endif()
