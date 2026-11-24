# Embed the PNGs so an installed game can resolve prompts from any working directory.
file(GLOB icon_files CONFIGURE_DEPENDS RELATIVE "${KIRBY_PROJECT_ROOT}/runtime/assets/button_icons"
    "${KIRBY_PROJECT_ROOT}/runtime/assets/button_icons/*/*.png")
set(icon_bytes "")
set(icon_table "")
foreach(icon IN LISTS icon_files)
    set(path "${KIRBY_PROJECT_ROOT}/runtime/assets/button_icons/${icon}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" bytes HEX)
    string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
    string(REPLACE ".png" "" name "${icon}")
    string(MAKE_C_IDENTIFIER "icon_${name}" symbol)
    string(APPEND icon_bytes "static constexpr uint8_t ${symbol}[] = {${bytes}};\n")
    string(APPEND icon_table "    {\"${name}\", ${symbol}},\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/kirby-generated/button_icons.inc"
    CONTENT "${icon_bytes}static constexpr Icon icons[] = {\n${icon_table}};\n")
