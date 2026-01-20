cmake_minimum_required(VERSION 3.30)
project(CCDB C CXX)

set(CMAKE_CXX_STANDARD 23)

# readline only supports Makefile
find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)
include(ExternalProject)

# Ncurses

set(NCURSES_MAKE_ENTIRE "${MAKE_EXECUTABLE} ${NCURSES_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "NCRUSES: ${NCURSES_MAKE_ENTIRE}")
ExternalProject_Add(NCURSES
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/ncurses
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/NonCMake/ncurses-6.6
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/ncurses/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --without-shared ${NCURSES_CONFIGURE_ADDITIONAL_FLAGS}
        BUILD_COMMAND       /bin/sh -c "${NCURSES_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${NCURSES_MAKE_INSTALL_ADDITIONAL_FLAGS}
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
add_library(ncurses::ncursesw STATIC IMPORTED GLOBAL)
set_target_properties(ncurses::ncursesw PROPERTIES
        IMPORTED_LOCATION             "${NCURSES_LIB_DIR}/libncursesw.a"
        INTERFACE_INCLUDE_DIRECTORIES "${NCURSES_INCLUDE_DIR}"
)

add_library(ncurses::ncurses INTERFACE IMPORTED GLOBAL)
target_link_libraries(ncurses::ncurses INTERFACE ncurses::ncursesw)

add_dependencies(ncurses::ncurses NCURSES)
include_directories("${NCURSES_INCLUDE_DIR}/ncursesw")
include_directories("${NCURSES_INCLUDE_DIR}")

# Readline

set(READLINE_MAKE_ENTIRE "${MAKE_EXECUTABLE} ${READLINE_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "READLINE: ${READLINE_MAKE_ENTIRE}")
ExternalProject_Add(READLINE
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/readline
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/NonCMake/readline-8.3
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/readline/install
        CONFIGURE_COMMAND   /bin/sh -c
                                "CPPFLAGS='-DNCURSES_STATIC -I${NCURSES_INCLUDE_DIR} -I${NCURSES_INCLUDE_DIR}/ncursesw' \
                                LDFLAGS='-L${NCURSES_LIB_DIR}' \
                                <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --with-curses ${READLINE_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "${READLINE_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${READLINE_MAKE_INSTALL_ADDITIONAL_FLAGS}
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
add_library(readline::readlineas STATIC IMPORTED GLOBAL)
set_target_properties(readline::readlineas PROPERTIES
        IMPORTED_LOCATION               "${READLINE_LIB_DIR}/libreadline.a"
        INTERFACE_INCLUDE_DIRECTORIES   "${READLINE_INCLUDE_DIR}"
)

add_library(readline::libhistory STATIC IMPORTED GLOBAL)
set_target_properties(readline::libhistory PROPERTIES
        IMPORTED_LOCATION               "${READLINE_LIB_DIR}/libhistory.a"
        INTERFACE_INCLUDE_DIRECTORIES   "${READLINE_INCLUDE_DIR}"
)

# Bundle target consumers can link to
add_library(readline::readline INTERFACE IMPORTED GLOBAL)
target_link_libraries(readline::readline INTERFACE
        readline::readlineas
        readline::libhistory
)

add_dependencies(readline::readline READLINE)
add_dependencies(READLINE NCURSES)
include_directories("${READLINE_INCLUDE_DIR}/readline")
include_directories("${READLINE_INCLUDE_DIR}")

add_library(lz4 STATIC
        include/lz4.h         lz4/lz4.c
        include/lz4frame.h    lz4/lz4frame.c
        include/lz4hc.h       lz4/lz4hc.c
        include/xxhash.h      lz4/xxhash.c)
add_executable(compress compile_utils/compress.cpp)
add_executable(decompress compile_utils/decompress.cpp)
target_include_directories(compress PRIVATE include)
target_include_directories(decompress PRIVATE include)
target_include_directories(lz4 PRIVATE include)
target_link_libraries(compress PRIVATE lz4)
target_link_libraries(decompress PRIVATE lz4)

find_program(XXD_EXEC   xxd     REQUIRED)
find_program(SED_EXEC   sed     REQUIRED)
find_program(GREP_EXEC  grep    REQUIRED)
find_program(CAT_EXEC   cat     REQUIRED)

function(generate_from_resource_file RES OUT_CPP OUT_H XXD_NAME)
    set(_in   "${CMAKE_SOURCE_DIR}/${RES}")
    set(_outc "${CMAKE_CURRENT_BINARY_DIR}/${OUT_CPP}")
    set(_outh "${CMAKE_CURRENT_BINARY_DIR}/${OUT_H}")

    add_custom_command(
        OUTPUT "${_outc}" "${_outh}"
        COMMAND "${CMAKE_SOURCE_DIR}/script/gen_header.sh" "${_outc}" "${_outh}" "${XXD_NAME}" "${_in}" "${CMAKE_CURRENT_BINARY_DIR}/compress"
        DEPENDS "${_in}" "${CMAKE_SOURCE_DIR}/script/gen_header.sh" compress
        VERBATIM
    )

    add_library(resource_${RES} STATIC "${_outc}" "${_outh}")
endfunction()

generate_from_resource_file(command.readline command.readline.cpp command.readline.h command_readline)

# CCDB
add_executable(ccdb
        main.cpp
        include/httplib.h
        general_info_pulling.cpp    include/general_info_pulling.h
        mihomo.cpp                  include/mihomo.h
        commandTemplateTree.cpp     include/commandTemplateTree.h
        misc/color.cpp              include/colors.h
        misc/utils.cpp              include/utils.h
        misc/execute.cpp
        ccdb.cpp                    include/ccdb.h
        term_name.cpp               include/term_name.h
        update-self.cpp             include/update-self.h
        config.cpp                  include/config.h
)
target_include_directories(ccdb PRIVATE ./include ${READLINE_INCLUDE_DIR} ${CMAKE_BINARY_DIR})
target_compile_definitions(ccdb PRIVATE CCDB_VERSION="0.8.4-rc5")
target_link_libraries(ccdb PRIVATE resource_command.readline lz4)

find_program(GIT_EXEC NAMES git REQUIRED)
find_program(HEAD_EXEC NAMES head REQUIRED)
execute_process(
        COMMAND /bin/sh -c "git ls-remote \"${CMAKE_SOURCE_DIR}\"/.. HEAD | head -c 8"
        OUTPUT_VARIABLE GIT_HASH
)

if ("X${GIT_HASH}" STREQUAL "X")
    set(GIT_HASH "Unknown")
endif ()

add_compile_definitions(GIT_HASH="${GIT_HASH}")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    if ("${CCDB_CYGWIN_BUILD}" STREQUAL "True")
        message(FATAL_ERROR "CCDB for Cygwin CANNOT be build on debug mode")
    endif ()
    
    if ("${CMAKE_SANITIZER_TYPE}" STREQUAL "address")
        target_compile_options(ccdb PRIVATE -fsanitize=address -fsanitize=undefined)
        target_link_libraries(ccdb PRIVATE asan ubsan atomic readline::readline ncurses::ncurses)
    elseif ("${CMAKE_SANITIZER_TYPE}" STREQUAL "thread")
        target_compile_options(ccdb PRIVATE -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
        target_link_libraries(ccdb PRIVATE tsan atomic readline::readline ncurses::ncurses)
    else()
        target_link_libraries(ccdb PRIVATE atomic readline::readline ncurses::ncurses)
    endif()

    add_executable(data_race_test data_race.cpp)
    target_compile_options(data_race_test PRIVATE -fsanitize=thread -fno-sanitize-recover=undefined -fno-sanitize=undefined)
    target_link_libraries(data_race_test PRIVATE tsan)
    target_include_directories(data_race_test PRIVATE include)
else ()
    set(LD_STATIC "")
    if ("${CMAKE_BUILD_STATIC}" STREQUAL "True")
        set(LD_STATIC "-static-libgcc;-static-libstdc++")
        set_target_properties(ccdb PROPERTIES LINK_SEARCH_START_STATIC 1)
        set_target_properties(ccdb PROPERTIES LINK_SEARCH_END_STATIC 1)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    else ()
        message(WARNING "GLIBC IS DYNAMICALLY LINKED!")
    endif ()
    target_compile_options(ccdb PRIVATE
            -O3
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -D_FORTIFY_SOURCE=2
            #-fno-stack-protector
            -fstack-protector-strong
            -s
            ${CC_ADDITIONAL_OPTIONS}
    )
    target_link_options(ccdb PRIVATE
            ${LD_STATIC}
            -O3
            -fomit-frame-pointer
            -ffast-math
            -fstrict-aliasing
            -fdata-sections
            -ffunction-sections
            -Wl,--gc-sections
            -D_FORTIFY_SOURCE=2
            #-fno-stack-protector
            -fstack-protector-strong
            -s
            ${LD_ADDITIONAL_OPTIONS}
    )
    target_link_libraries(ccdb PRIVATE atomic readline::readline ncurses::ncurses)
    add_dependencies(ccdb READLINE NCURSES)
    if ("${CCDB_CYGWIN_BUILD}" STREQUAL "True")
        target_compile_definitions(ccdb PRIVATE _CCDB_CYGWIN_BUILD_=1)
    endif ()

    target_compile_definitions(ccdb PRIVATE NCURSES_STATIC)
endif ()
