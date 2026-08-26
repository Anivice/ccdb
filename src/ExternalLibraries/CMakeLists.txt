
# Perl
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/ExternalLibraries/perl-5.42.0/p)
ExternalProject_Add(PERL
        PREFIX              ${CMAKE_BINARY_DIR}/perl
        SOURCE_DIR          ${CMAKE_BINARY_DIR}/ExternalLibraries/perl-5.42.0
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/perl/install
        CONFIGURE_COMMAND   rm -rf <SOURCE_DIR> && cp -r ${CMAKE_SOURCE_DIR}/ExternalLibraries/perl-5.42.0 <SOURCE_DIR> && cd <SOURCE_DIR> && sh -c "${PerlEnv} <SOURCE_DIR>/configure.gnu --prefix=<INSTALL_DIR> ${PERL_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       sh -c "cd <SOURCE_DIR> && ${PerlEnv} ${MAKE_EXECUTABLE} ${PERL_MAKE_ADDITIONAL_FLAGS} ${PerlTail}"
        INSTALL_COMMAND     sh -c "cd <SOURCE_DIR> && ${MAKE_EXECUTABLE} install ${PERL_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
)

set(OPENSSL_TARGET "${OPENSSL_TARGET}")
if ("${OPENSSL_TARGET}" STREQUAL "")
    set(OPENSSL_TARGET linux-${CMAKE_C_COMPILER_ARCHITECTURE_ID})
endif ()

set(OPENSSL_LIBP "${OPENSSL_LIBP}")
if ("${OPENSSL_LIBP}" STREQUAL "")
    set(OPENSSL_LIBP lib64)
endif ()

# OpenSSL
set(OPENSSL_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${OPENSSL_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "OpenSSL: ${OPENSSL_MAKE_ENTIRE}")
ExternalProject_Add(OPENSSL
        PREFIX              ${CMAKE_BINARY_DIR}/openssl
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/openssl-4.0.2
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/openssl/install
        CONFIGURE_COMMAND   /bin/sh -c "env PERL5LIB=\"${CMAKE_BINARY_DIR}/perl/install/lib/5.42.0/\" PATH=\"${CMAKE_BINARY_DIR}/perl/install/bin:$PATH\" <SOURCE_DIR>/Configure ${OPENSSL_TARGET} no-shared no-docs no-tests --prefix=<INSTALL_DIR> --openssldir=\"<INSTALL_DIR>/ssl\" ${OPENSSL_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "${OPENSSL_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${OPENSSL_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/${OPENSSL_LIBP}/libcrypto.a  <INSTALL_DIR>/${OPENSSL_LIBP}/libssl.a
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
        DEPENDS PERL
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/openssl/install/include" "${CMAKE_BINARY_DIR}/openssl/install/lib64/")

# Export to CMake
add_library(openssl::libcrypto STATIC IMPORTED GLOBAL)
set_target_properties(openssl::libcrypto PROPERTIES
        IMPORTED_LOCATION             "${CMAKE_BINARY_DIR}/openssl/install/${OPENSSL_LIBP}/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/openssl/install/include"
)

add_library(openssl::libssl STATIC IMPORTED GLOBAL)
set_target_properties(openssl::libssl PROPERTIES
        IMPORTED_LOCATION             "${CMAKE_BINARY_DIR}/openssl/install/${OPENSSL_LIBP}/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/openssl/install/include"
)

add_library(openssl::openssl INTERFACE IMPORTED GLOBAL)
target_link_libraries(openssl::openssl INTERFACE
        openssl::libssl
        openssl::libcrypto
)

# ICU
set(ICU_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${ICU_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "ICU: ${ICU_MAKE_ENTIRE}")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/ExternalLibraries/icu4c/p)
ExternalProject_Add(ICU
        PREFIX              ${CMAKE_BINARY_DIR}/icu4c
        SOURCE_DIR          ${CMAKE_BINARY_DIR}/ExternalLibraries/icu4c
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/icu4c/install
        CONFIGURE_COMMAND   rm -rf <SOURCE_DIR> && cp -r ${CMAKE_SOURCE_DIR}/ExternalLibraries/icu4c-78.3/ <SOURCE_DIR> && cd <SOURCE_DIR> && sh -c "CXXFLAGS='-fPIC -std=c++17' CFLAGS='-fPIC -std=c11' source/runConfigureICU Linux/gcc --prefix=<INSTALL_DIR> --disable-shared --enable-static"
        BUILD_COMMAND       sh -c "cd <SOURCE_DIR> && ${ICU_MAKE_ENTIRE}"
        INSTALL_COMMAND     sh -c "cd <SOURCE_DIR> && ${MAKE_EXECUTABLE} install ${ICU_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libicudata.a <INSTALL_DIR>/lib/libicui18n.a <INSTALL_DIR>/lib/libicutest.a <INSTALL_DIR>/lib/libicutu.a <INSTALL_DIR>/lib/libicuuc.a
)

ExternalProject_Get_Property(ICU INSTALL_DIR)
set(ICU_INCLUDE_DIR "${INSTALL_DIR}/include")
set(ICU_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${ICU_INCLUDE_DIR}" "${ICU_LIB_DIR}")
set(ICU_PKGCONFIG_DIR "${INSTALL_DIR}/lib/pkgconfig")

# Export to CMake
add_library(icu4c::libicudata STATIC IMPORTED GLOBAL)
set_target_properties(icu4c::libicudata PROPERTIES
        IMPORTED_LOCATION             "${ICU_LIB_DIR}/libicudata.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
)

add_library(icu4c::libicui18n STATIC IMPORTED GLOBAL)
set_target_properties(icu4c::libicui18n PROPERTIES
        IMPORTED_LOCATION             "${ICU_LIB_DIR}/libicui18n.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
)

add_library(icu4c::libicutest STATIC IMPORTED GLOBAL)
set_target_properties(icu4c::libicutest PROPERTIES
        IMPORTED_LOCATION             "${ICU_LIB_DIR}/libicutest.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
)

add_library(icu4c::libicutu STATIC IMPORTED GLOBAL)
set_target_properties(icu4c::libicutu PROPERTIES
        IMPORTED_LOCATION             "${ICU_LIB_DIR}/libicutu.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
)

add_library(icu4c::libicuuc STATIC IMPORTED GLOBAL)
set_target_properties(icu4c::libicuuc PROPERTIES
        IMPORTED_LOCATION             "${ICU_LIB_DIR}/libicuuc.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ICU_INCLUDE_DIR}"
)

include_directories("${ICU_INCLUDE_DIR}")

add_library(icu4c::icu4c INTERFACE IMPORTED GLOBAL)
target_link_libraries(icu4c::icu4c INTERFACE
        icu4c::libicuuc
        icu4c::libicudata
        icu4c::libicui18n
        icu4c::libicutu
        icu4c::libicutest
)

add_dependencies(icu4c::icu4c ICU)

# libpsl

set(LIBPSL_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${LIBPSL_MAKE_ADDITIONAL_FLAGS}")
set(PSL_SRC_DEST ${CMAKE_BINARY_DIR}/ExternalLibraries/libpsl)
message(STATUS "LIBPSL: ${LIBPSL_MAKE_ENTIRE}")
ExternalProject_Add(LIBPSL
        PREFIX              ${CMAKE_BINARY_DIR}/libpsl
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/libpsl-0.23.3
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/libpsl/install
        CONFIGURE_COMMAND   /bin/sh -c "rm -rf ${PSL_SRC_DEST}/ && mkdir -p ${PSL_SRC_DEST}/ && cp -r <SOURCE_DIR>/* ${PSL_SRC_DEST}/ && cd ${PSL_SRC_DEST} && autoreconf -f && env LIBS=-lstdc++ PKG_CONFIG_PATH=\"${ICU_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}\" ${PSL_SRC_DEST}/configure --prefix=<INSTALL_DIR> --enable-runtime=libicu --disable-shared --enable-static ${LIBPSL_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "cd ${PSL_SRC_DEST} && ${LIBPSL_MAKE_ENTIRE} CC=\"${MAKE_CC_EXEC}\" CFLAGS=-fPIC CXXFLAGS=-fPIC CXX=\"${MAKE_CXX_EXEC}\""
        INSTALL_COMMAND     /bin/sh -c "cd ${PSL_SRC_DEST} && ${MAKE_EXECUTABLE} install ${LIBPSL_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libpsl.a
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
        DEPENDS ICU
)

ExternalProject_Get_Property(LIBPSL INSTALL_DIR)
set(LIBPSL_INCLUDE_DIR "${INSTALL_DIR}/include")
set(LIBPSL_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${LIBPSL_INCLUDE_DIR}" "${LIBPSL_LIB_DIR}")

# Export to CMake
add_library(libpsl::libpsl STATIC IMPORTED GLOBAL)
set_target_properties(libpsl::libpsl PROPERTIES
        IMPORTED_LOCATION             "${LIBPSL_LIB_DIR}/libpsl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBPSL_INCLUDE_DIR}"
)

include_directories("${LIBPSL_INCLUDE_DIR}")

# Ncurses

set(NCURSES_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${NCURSES_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "NCURSES: ${NCURSES_MAKE_ENTIRE}")
ExternalProject_Add(NCURSES
        PREFIX              ${CMAKE_BINARY_DIR}/ncurses
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/ncurses-6.6
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/ncurses/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --without-shared --without-tests --without-cxx-binding ${NCURSES_CONFIGURE_ADDITIONAL_FLAGS}
        BUILD_COMMAND       /bin/sh -c "${NCURSES_MAKE_ENTIRE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" CFLAGS=-fPIC CXXFLAGS=-fPIC"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${NCURSES_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libncursesw.a
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
        #        DEPENDS OPENSSL
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

#add_dependencies(ncurses::ncurses NCURSES)
include_directories("${NCURSES_INCLUDE_DIR}/ncursesw")
include_directories("${NCURSES_INCLUDE_DIR}")

# Readline

set(READLINE_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${READLINE_MAKE_ADDITIONAL_FLAGS} CFLAGS=-fPIC CXXFLAGS=-fPIC")
message(STATUS "READLINE: ${READLINE_MAKE_ENTIRE}")
ExternalProject_Add(READLINE
        PREFIX              ${CMAKE_BINARY_DIR}/readline
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/readline-8.3
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/readline/install
        CONFIGURE_COMMAND   /bin/sh -c
        "CPPFLAGS='-DNCURSES_STATIC -I${NCURSES_INCLUDE_DIR} -I${NCURSES_INCLUDE_DIR}/ncursesw -fPIE' \
                                LDFLAGS='-L${NCURSES_LIB_DIR}' \
                                <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --with-curses ${READLINE_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "${READLINE_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${READLINE_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libhistory.a <INSTALL_DIR>/lib/libreadline.a # <INSTALL_DIR>/lib/libreadline.so <INSTALL_DIR>/lib/libhistory.so
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
        DEPENDS NCURSES
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

include_directories("${READLINE_INCLUDE_DIR}/readline")
include_directories("${READLINE_INCLUDE_DIR}")

# Embedded tar
set(TAR_LD_FLAGS "")
set(TAR_C_FLAGS "")
if ("${CMAKE_BUILD_STATIC}" STREQUAL "True")
    if (NOT "${CLANG}" STREQUAL "True")
        set(TAR_LD_FLAGS "-static-libgcc -static-libstdc++ ")
    endif ()
    set(TAR_LD_FLAGS "${TAR_LD_FLAGS} -static")
    set(TAR_C_FLAGS "-O2 -static")
endif ()

set(TAR_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${TAR_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "TAR: ${TAR_MAKE_ENTIRE}")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/ExternalLibraries/tar-1.35/p)
set(tar_dest "${CMAKE_BINARY_DIR}/ExternalLibraries/tar-1.35/")
ExternalProject_Add(TAR
        PREFIX              ${CMAKE_BINARY_DIR}/tar
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/tar-1.35
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/tar/install
        CONFIGURE_COMMAND   /bin/sh -c "cp -r <SOURCE_DIR>/* \"${tar_dest}\" && cd \"${tar_dest}\" && autoreconf -f && env MAKEINFO=true CC=\"${CMAKE_C_COMPILER}\" LD=\"${CMAKE_LINKER}\" CFLAGS=\"${TAR_C_FLAGS}\" LDFLAGS=\"${TAR_LD_FLAGS}\" \"${tar_dest}\"/configure --prefix=<INSTALL_DIR> --without-gzip --without-bzip2 --without-xz --without-zstd --without-lzip --without-lzma --without-lzop --without-compress --disable-nls ${TAR_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "cd \"${tar_dest}\" && ${TAR_MAKE_ENTIRE} CFLAGS=\"${TAR_C_FLAGS}\" LDFLAGS=\"${TAR_LD_FLAGS}\""
        INSTALL_COMMAND     /bin/sh -c "cd \"${tar_dest}\" && ${MAKE_EXECUTABLE} install ${TAR_MAKE_INSTALL_ADDITIONAL_FLAGS} && STRIP=${CMAKE_STRIP} ${CMAKE_SOURCE_DIR}/script/strip.sh \"<INSTALL_DIR>/bin/tar\""
        TEST_COMMAND        <INSTALL_DIR>/bin/tar --version
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/bin/tar
)

# libmaxmind GeoIP Database

set(MAXMIND_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${MAXMIND_MAKE_ADDITIONAL_FLAGS} CFLAGS=-fPIC CXXFLAGS=-fPIC")
message(STATUS "MAXMIND: ${MAXMIND_MAKE_ENTIRE}")
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/p)
ExternalProject_Add(MAXMIND
        PREFIX              ${CMAKE_BINARY_DIR}/libmaxminddb
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/libmaxminddb-1.13.3
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/libmaxminddb/install
        CONFIGURE_COMMAND   /bin/sh -c "rm -rf ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb && mkdir -p ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb && cp -r <SOURCE_DIR>/* ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && cd ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && autoreconf -f && ./configure --prefix=<INSTALL_DIR> ${MAXMIND_CONFIGURE_ADDITIONAL_FLAGS} --enable-static --disable-shared --disable-tests"
        BUILD_COMMAND       /bin/sh -c "cd ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && ${MAXMIND_MAKE_ENTIRE}"
        INSTALL_COMMAND     /bin/sh -c "cd ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && ${MAKE_EXECUTABLE} install ${MAXMIND_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libmaxminddb.a
        TEST_COMMAND        ""
        INACTIVITY_TIMEOUT  15
        CONFIGURE_HANDLED_BY_BUILD ON
)

ExternalProject_Get_Property(MAXMIND INSTALL_DIR)
set(MAXMIND_INCLUDE_DIR "${INSTALL_DIR}/include")
set(MAXMIND_LIB_DIR     "${INSTALL_DIR}/lib")
file(MAKE_DIRECTORY "${MAXMIND_INCLUDE_DIR}" "${MAXMIND_LIB_DIR}")

# Export to CMake
add_library(maxmind::libmaxminddb STATIC IMPORTED GLOBAL)
set_target_properties(maxmind::libmaxminddb PROPERTIES
        IMPORTED_LOCATION               "${MAXMIND_LIB_DIR}/libmaxminddb.a"
        INTERFACE_INCLUDE_DIRECTORIES   "${MAXMIND_INCLUDE_DIR}"
)

add_library(maxmind::libmaxmind INTERFACE IMPORTED GLOBAL)
target_link_libraries(maxmind::libmaxmind INTERFACE maxmind::libmaxminddb)
include_directories("${MAXMIND_INCLUDE_DIR}")