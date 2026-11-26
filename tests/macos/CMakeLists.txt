cmake_minimum_required(VERSION 3.25)
project(KirbyHostChecks LANGUAGES CXX)
enable_testing()
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${ROOT}/cmake/apple_threads.cmake")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_path(JSON_INCLUDE nlohmann/json.hpp REQUIRED)
find_path(CRYPTOPP_INCLUDE cryptopp/sha.h REQUIRED)
find_library(CRYPTOPP_LIBRARY NAMES cryptopp crypto++ REQUIRED)
include_directories("${ROOT}/runtime/include" "${ROOT}/launcher" "${JSON_INCLUDE}" "${CRYPTOPP_INCLUDE}")
add_compile_options(-Wall -Wextra -Werror)
function(host_check name test)
    add_executable(${name} "${ROOT}/${test}" ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()
set(nand "${ROOT}/runtime/src/services/nand.cpp")
set(ios "${ROOT}/runtime/src/services/ios_service.cpp")
set(disc "${ROOT}/runtime/src/services/disc.cpp")
set(es "${ROOT}/runtime/src/services/es_metadata.cpp")
host_check(nand tests/unit/services/nand_test.cpp ${nand})
host_check(disc tests/unit/services/disc_test.cpp ${disc} ${nand} ${ios} ${es})
host_check(ios_nand tests/unit/services/ios_nand_test.cpp ${ios} ${nand} ${disc} ${es})
host_check(frame_log tests/unit/frame_log_test.cpp "${ROOT}/runtime/src/app/frame_log.cpp")
host_check(command_line tests/unit/command_line_test.cpp)
host_check(readback_wait tests/unit/graphics/readback_wait_test.cpp)
host_check(setup_job tests/unit/setup_job_test.cpp "${ROOT}/launcher/setup/job.cpp" "${ROOT}/launcher/setup/protocol.cpp")
host_check(setup_installation tests/unit/setup_installation_test.cpp "${ROOT}/launcher/setup/installation.cpp")
target_link_libraries(setup_installation PRIVATE "${CRYPTOPP_LIBRARY}")
host_check(save_manager tests/unit/save_manager_test.cpp "${ROOT}/launcher/save_manager.cpp")
target_link_libraries(save_manager PRIVATE "${CRYPTOPP_LIBRARY}")
set_tests_properties(setup_job PROPERTIES TIMEOUT 30)

# Compile the real launcher without a game DOL. This catches Darwin-specific
# filesystem/process failures before asking a tester to translate the game.
if(KIRBY_UPSTREAM_RUNTIME)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${ROOT}/tools/build_info.py"
        --source "${ROOT}" --output "${CMAKE_CURRENT_BINARY_DIR}/KirbyBuildInfo.h"
        --compiler "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        --build-type Release COMMAND_ERROR_IS_FATAL ANY)
    add_executable(kirby-launcher
        "${ROOT}/launcher/main.cpp" "${ROOT}/launcher/desktop_service.cpp"
        "${ROOT}/launcher/language.cpp" "${ROOT}/launcher/build_probe.cpp"
        "${ROOT}/launcher/binding_capture.cpp" "${ROOT}/launcher/save_manager.cpp"
        "${ROOT}/launcher/setup/protocol.cpp" "${ROOT}/launcher/setup/job.cpp"
        "${ROOT}/launcher/setup/installation.cpp"
        "${ROOT}/runtime/src/app/settings.cpp" "${ROOT}/runtime/src/app/build_info.cpp"
        "${ROOT}/runtime/src/input/bindings.cpp" "${ROOT}/runtime/src/input/motion_device.cpp"
        "${KIRBY_UPSTREAM_RUNTIME}/src/platform/host_platform.cpp")
    target_include_directories(kirby-launcher PRIVATE "${KIRBY_UPSTREAM_RUNTIME}/include"
        "${KIRBY_UPSTREAM_RUNTIME}/third_party/toml11" "${CMAKE_CURRENT_BINARY_DIR}"
        "${KIRBY_UPSTREAM_RUNTIME}/../aurora-main/include")
    target_compile_definitions(kirby-launcher PRIVATE SDL_MAIN_HANDLED TARGET_PC)
    # The production launcher intentionally value-initializes trailing aggregate
    # fields. Keep other warnings fatal without rejecting that existing style.
    target_compile_options(kirby-launcher PRIVATE -Wno-missing-field-initializers)
    target_link_libraries(kirby-launcher PRIVATE SDL3::SDL3 "${CRYPTOPP_LIBRARY}")
    add_test(NAME launcher_build_info COMMAND kirby-launcher --build-info)
endif()
