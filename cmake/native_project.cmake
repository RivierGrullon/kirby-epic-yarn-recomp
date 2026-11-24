if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-std=c++20")
    set(_kirby_thread_probe [[
        #include <stop_token>
        #include <thread>
        int main() {
            std::jthread worker([](std::stop_token) {});
        }
    ]])
    check_cxx_source_compiles("${_kirby_thread_probe}" KIRBY_APPLE_HAS_JTHREAD)
    if(NOT KIRBY_APPLE_HAS_JTHREAD)
        set(CMAKE_REQUIRED_FLAGS "-std=c++20 -fexperimental-library")
        check_cxx_source_compiles("${_kirby_thread_probe}" KIRBY_APPLE_HAS_EXPERIMENTAL_JTHREAD)
        if(NOT KIRBY_APPLE_HAS_EXPERIMENTAL_JTHREAD)
            message(FATAL_ERROR "Apple Clang cannot compile std::jthread. Update the Xcode Command Line Tools.")
        endif()
        # SDKs that gate libc++'s C++20 thread API also need its experimental
        # library linked anywhere that consumes Aurora's native objects.
        add_compile_options($<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fexperimental-library>)
        add_link_options($<$<LINK_LANGUAGE:CXX,OBJCXX>:-fexperimental-library>)
    endif()
    unset(_kirby_thread_probe)
    cmake_pop_check_state()
endif()

# Run after upstream has declared its runtime and product targets.
cmake_language(DEFER CALL include "${KIRBY_PROJECT_ROOT}/cmake/native_targets.cmake")
