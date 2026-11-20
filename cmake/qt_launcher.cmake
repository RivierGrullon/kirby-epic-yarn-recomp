# The frontend is its own CMake project so the Qt kit and the game's C++ ABI
# never share a link step. A standalone build of launcher/qt works as well.
if(EXISTS "${KIRBY_PROJECT_ROOT}/build/native/qt-launcher/.bundled-revision")
    # Packaged Setup has already installed the release's relocatable frontend.
    return()
endif()
set(KIRBY_QT_PREFIX "" CACHE PATH "Qt 6 desktop kit prefix for the launcher")
set(KIRBY_QT_CXX "" CACHE FILEPATH "C++ compiler matching the Qt kit")
if(CMAKE_HOST_WIN32 AND WIN32)
    if(NOT KIRBY_QT_PREFIX)
        file(GLOB kits "C:/Qt/6.*/mingw_64")
        list(SORT kits COMPARE NATURAL ORDER DESCENDING)
        foreach(kit IN LISTS kits)
            if(EXISTS "${kit}/lib/cmake/Qt6/Qt6Config.cmake")
                set(KIRBY_QT_PREFIX "${kit}")
                break()
            endif()
        endforeach()
    endif()
    if(KIRBY_QT_PREFIX AND NOT KIRBY_QT_CXX)
        file(GLOB compilers "${KIRBY_QT_PREFIX}/../../Tools/mingw*/bin/g++.exe")
        list(SORT compilers COMPARE NATURAL ORDER DESCENDING)
        if(compilers)
            list(GET compilers 0 KIRBY_QT_CXX)
        endif()
    endif()
elseif(NOT CMAKE_CROSSCOMPILING AND NOT KIRBY_QT_PREFIX)
    find_program(kirby_qtpaths NAMES qtpaths6)
    if(kirby_qtpaths)
        execute_process(COMMAND "${kirby_qtpaths}" --query QT_INSTALL_PREFIX
            OUTPUT_VARIABLE KIRBY_QT_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
endif()
if(KIRBY_QT_PREFIX)
    if(NOT KIRBY_QT_CXX)
        set(KIRBY_QT_CXX "${CMAKE_CXX_COMPILER}")
    endif()
    get_filename_component(qt_compiler_bin "${KIRBY_QT_CXX}" DIRECTORY)
    if(CMAKE_HOST_WIN32)
        set(qt_path "${qt_compiler_bin};$ENV{PATH}")
    else()
        set(qt_path "${qt_compiler_bin}:$ENV{PATH}")
    endif()
    set(qt_build "${CMAKE_CURRENT_BINARY_DIR}/kirby-qt-build")
    add_custom_target(kirby_qt_frontend
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${qt_path}" "${CMAKE_COMMAND}"
            -S "${KIRBY_PROJECT_ROOT}/launcher/qt" -B "${qt_build}" -G Ninja
            "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" "-DCMAKE_CXX_COMPILER=${KIRBY_QT_CXX}"
            "-DCMAKE_PREFIX_PATH=${KIRBY_QT_PREFIX}" "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/qt-launcher"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${qt_path}" "${CMAKE_COMMAND}" --build "${qt_build}"
        VERBATIM)
    add_dependencies(kirby-launcher kirby_qt_frontend)
endif()
