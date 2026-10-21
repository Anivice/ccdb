file(GLOB readline_sources CONFIGURE_DEPENDS readline/*.c compat/*.c)
list(FILTER readline_sources EXCLUDE REGEX "/(emacs_keymap|vi_keymap)\\.c$")
add_library(ccdb_readline STATIC ${readline_sources})
target_include_directories(ccdb_readline
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/readline")
target_compile_definitions(ccdb_readline PRIVATE
    HAVE_CONFIG_H HANDLE_MULTIBYTE BUILD_READLINE
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
target_compile_options(ccdb_readline PRIVATE /utf-8)
set_property(TARGET ccdb_readline PROPERTY C_STANDARD 11)
