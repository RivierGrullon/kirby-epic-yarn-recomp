# Translate only the object-format directives in an owned build copy. The Linux
# translator emits ELF assembly; the embedded bytes and public symbols are also
# valid for Windows x86-64 once the read-only section uses COFF syntax.
function(kirby_use_windows_data_blob target source)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" content)
    if(NOT content MATCHES "\\.section \\.rodata" AND NOT content MATCHES "\\.section \\.rdata")
        message(FATAL_ERROR "Unrecognized generated data section format: ${source}")
    endif()
    string(REPLACE ".section .rodata,\"a\",@progbits" ".section .rdata,\"dr\"" content "${content}")
    string(REPLACE ".section .note.GNU-stack,\"\",@progbits" "" content "${content}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/kirby_data_sections_windows.S")
    file(CONFIGURE OUTPUT "${output}" CONTENT "${content}" @ONLY)
    get_target_property(sources ${target} SOURCES)
    list(REMOVE_ITEM sources "${source}")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    target_sources(${target} PRIVATE "${output}")
    set_source_files_properties("${output}" PROPERTIES LANGUAGE ASM SKIP_UNITY_BUILD_INCLUSION ON)
endfunction()
