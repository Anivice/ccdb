cmake_minimum_required(VERSION 3.30)
project(src)

set(CMAKE_CXX_STANDARD 23)

# readline only supports Makefile
find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)
include(ExternalProject)

ExternalProject_Add(NCURSES
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/ncurses
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/NonCMake/ncurses-6.5
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/ncurses/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR>
        BUILD_COMMAND       ${MAKE_EXECUTABLE} -j
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} -j install
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libncursesw.a
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
)

ExternalProject_Get_Property(NCURSES INSTALL_DIR)
set(NCURSES_INCLUDE_DIR "${INSTALL_DIR}/include")
set(NCURSES_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${NCURSES_INCLUDE_DIR}" "${NCURSES_LIB_DIR}")

# Export to CMake
add_library(ncurses::ncurses UNKNOWN IMPORTED)
set_target_properties(ncurses::ncurses PROPERTIES
        IMPORTED_LOCATION             "${NCURSES_LIB_DIR}/libncursesw.a" # <= ncurses lib location
        INTERFACE_INCLUDE_DIRECTORIES "${NCURSES_INCLUDE_DIR}"
)
add_dependencies(ncurses::ncurses NCURSES)
include_directories("${NCURSES_INCLUDE_DIR}/ncursesw")
include_directories("${NCURSES_INCLUDE_DIR}")

ExternalProject_Add(READLINE
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/readline
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/NonCMake/readline-8.3
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/readline/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --with-curses
        BUILD_COMMAND       ${MAKE_EXECUTABLE} -j
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} -j install
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libhistory.a <INSTALL_DIR>/lib/libreadline.a <INSTALL_DIR>/lib/libreadline.so <INSTALL_DIR>/lib/libhistory.so
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
)

ExternalProject_Get_Property(READLINE INSTALL_DIR)
set(READLINE_INCLUDE_DIR "${INSTALL_DIR}/include")
set(READLINE_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${READLINE_INCLUDE_DIR}" "${READLINE_LIB_DIR}")

# Export to CMake
add_library(readline::readline UNKNOWN IMPORTED)
set_target_properties(readline::readline PROPERTIES
        IMPORTED_LOCATION               "${READLINE_LIB_DIR}/libhistory.a"
        IMPORTED_LOCATION               "${READLINE_LIB_DIR}/libreadline.a"
        INTERFACE_INCLUDE_DIRECTORIES   "${READLINE_INCLUDE_DIR}"
)
add_dependencies(readline::readline READLINE)
include_directories("${READLINE_INCLUDE_DIR}/readline")
include_directories("${READLINE_INCLUDE_DIR}")

# CCDB
add_executable(ccdb
        main.cpp
        include/httplib.h
        general_info_pulling.cpp    include/general_info_pulling.h
        mihomo.cpp                  include/mihomo.h
)
target_include_directories(ccdb PRIVATE ./include ${READLINE_INCLUDE_DIR})
target_compile_definitions(ccdb PUBLIC VERSION="0.0.1")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
#    target_compile_options(ccdb PUBLIC -fsanitize=address -fsanitize=undefined)
#    target_link_libraries(ccdb PUBLIC asan ubsan atomic readline::readline ncurses::ncurses)
    target_compile_options(ccdb PUBLIC -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
    target_link_libraries(ccdb PUBLIC tsan atomic readline::readline ncurses::ncurses)
else ()
    target_compile_options(ccdb PUBLIC
            -O3
            # -flto
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -D_FORTIFY_SOURCE=2
            -fstack-protector-strong
            -Wl,-z,relro -Wl,-z,now
            -s
            ${CC_ADDITIONAL_OPTIONS}
    )
    target_link_options(ccdb PUBLIC
            -static-libgcc -static-libstdc++
            -O3
            # -flto
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -Wl,--gc-sections
            -D_FORTIFY_SOURCE=2
            -fstack-protector-strong
            -Wl,-z,relro -Wl,-z,now
            -s
            ${LD_ADDITIONAL_OPTIONS}
    )
    target_link_libraries(ccdb PUBLIC atomic readline::readline ncurses::ncurses)
    set_target_properties(ccdb PROPERTIES LINK_SEARCH_START_STATIC 1)
    set_target_properties(ccdb PROPERTIES LINK_SEARCH_END_STATIC 1)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    add_dependencies(ccdb READLINE NCURSES)
endif ()
