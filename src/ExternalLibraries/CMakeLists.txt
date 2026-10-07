# EXTERNAL_PROJECT_OPTIONS contains ExternalProject_Add keywords (including
# command argv and DEPENDS), forwarded unchanged. Common timeout/configure
# settings are supplied here.
# Additional keywords:
#   PREPARE_DIRECTORIES        directories created before ExternalProject_Add
#   MAKE_DIRECTORIES           directories created after ExternalProject_Add
#   EXPORT_DIRECTORY_VARIABLES prefix for <prefix>_INCLUDE_DIR / _LIB_DIR;
#                              also preserves the caller's INSTALL_DIR variable
#   PKGCONFIG_VARIABLE         caller variable receiving <INSTALL_DIR>/lib/pkgconfig
#   IMPORTED_LIBRARIES          pairs of imported target name and archive path
#   BUNDLE_TARGET              optional imported INTERFACE target
#   BUNDLE_LIBRARIES           ordered libraries linked by that target
#   BUNDLE_DEPENDS             explicit dependencies of that target (no defaults)
#   GLOBAL_INCLUDE_DIRECTORIES ordered directory-wide include paths
# MAKE_DIRECTORIES, IMPORTED_LIBRARIES and GLOBAL_INCLUDE_DIRECTORIES paths
# may contain <INSTALL_DIR>. PREPARE_DIRECTORIES uses literal paths.
function(add_cmake_external_project_from_vendored_source_tarball name)
    message(STATUS "Project name: ${name}")

    cmake_parse_arguments(PARSE_ARGV 1 vendored
        ""
        "EXPORT_DIRECTORY_VARIABLES;PKGCONFIG_VARIABLE;BUNDLE_TARGET"
        "PREPARE_DIRECTORIES;MAKE_DIRECTORIES;IMPORTED_LIBRARIES;BUNDLE_LIBRARIES;BUNDLE_DEPENDS;GLOBAL_INCLUDE_DIRECTORIES;EXTERNAL_PROJECT_OPTIONS"
    )

    if(vendored_PREPARE_DIRECTORIES)
        file(MAKE_DIRECTORY ${vendored_PREPARE_DIRECTORIES})
    endif()

    ExternalProject_Add(${name}
        INACTIVITY_TIMEOUT 15
        CONFIGURE_HANDLED_BY_BUILD ON
        ${vendored_EXTERNAL_PROJECT_OPTIONS}
    )
    ExternalProject_Get_Property(${name} INSTALL_DIR)
    message(STATUS "Project options: `${vendored_EXTERNAL_PROJECT_OPTIONS}`")

    foreach(keyword IN ITEMS MAKE_DIRECTORIES IMPORTED_LIBRARIES GLOBAL_INCLUDE_DIRECTORIES)
        string(REPLACE "<INSTALL_DIR>" "${INSTALL_DIR}"
            vendored_${keyword} "${vendored_${keyword}}")
    endforeach()

    if(vendored_EXPORT_DIRECTORY_VARIABLES)
        set(INSTALL_DIR "${INSTALL_DIR}" PARENT_SCOPE)
        set(${vendored_EXPORT_DIRECTORY_VARIABLES}_INCLUDE_DIR "${INSTALL_DIR}/include" PARENT_SCOPE)
        set(${vendored_EXPORT_DIRECTORY_VARIABLES}_LIB_DIR "${INSTALL_DIR}/lib" PARENT_SCOPE)
    endif()
    if(vendored_PKGCONFIG_VARIABLE)
        set(${vendored_PKGCONFIG_VARIABLE} "${INSTALL_DIR}/lib/pkgconfig" PARENT_SCOPE)
    endif()
    if(vendored_MAKE_DIRECTORIES)
        file(MAKE_DIRECTORY ${vendored_MAKE_DIRECTORIES})
    endif()

    # Each pair allows the public target name to differ from the archive name.
    list(LENGTH vendored_IMPORTED_LIBRARIES library_count)
    math(EXPR library_remainder "${library_count} % 2")
    if(library_remainder)
        message(FATAL_ERROR "${name}: IMPORTED_LIBRARIES requires target/path pairs")
    endif()
    while(vendored_IMPORTED_LIBRARIES)
        list(POP_FRONT vendored_IMPORTED_LIBRARIES library_target library_path)
        add_library(${library_target} STATIC IMPORTED GLOBAL)
        set_target_properties(${library_target} PROPERTIES
            IMPORTED_LOCATION "${library_path}"
            INTERFACE_INCLUDE_DIRECTORIES "${INSTALL_DIR}/include"
        )
    endwhile()

    if(vendored_BUNDLE_TARGET)
        add_library(${vendored_BUNDLE_TARGET} INTERFACE IMPORTED GLOBAL)
        target_link_libraries(${vendored_BUNDLE_TARGET} INTERFACE ${vendored_BUNDLE_LIBRARIES})
        if(vendored_BUNDLE_DEPENDS)
            add_dependencies(${vendored_BUNDLE_TARGET} ${vendored_BUNDLE_DEPENDS})
        endif()
    endif()
    foreach(include_dir IN LISTS vendored_GLOBAL_INCLUDE_DIRECTORIES)
        include_directories("${include_dir}")
    endforeach()
endfunction()

# Perl
set(PerlEnv "env CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" LD= ")
set(PerlTail "")
add_cmake_external_project_from_vendored_source_tarball(PERL
    PREPARE_DIRECTORIES ${CMAKE_BINARY_DIR}/ExternalLibraries/perl-5.42.0/p
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/perl
        SOURCE_DIR          ${CMAKE_BINARY_DIR}/ExternalLibraries/perl-5.42.0
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/perl/install
        CONFIGURE_COMMAND   sh -c
                                "rm -rf <SOURCE_DIR> && \
                                cp -r ${CMAKE_SOURCE_DIR}/ExternalLibraries/perl-5.42.0 <SOURCE_DIR> && \
                                cd <SOURCE_DIR> && \
                                export ${PerlEnv} && \
                                <SOURCE_DIR>/configure.gnu --prefix=<INSTALL_DIR> ${PERL_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       sh -c "cd <SOURCE_DIR> && ${PerlEnv} ${MAKE_EXECUTABLE} ${PERL_MAKE_ADDITIONAL_FLAGS} ${PerlTail}"
        INSTALL_COMMAND     sh -c "cd <SOURCE_DIR> && ${MAKE_EXECUTABLE} install ${PERL_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        TEST_COMMAND        ""
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
add_cmake_external_project_from_vendored_source_tarball(OPENSSL
    MAKE_DIRECTORIES "${CMAKE_BINARY_DIR}/openssl/install/include" "${CMAKE_BINARY_DIR}/openssl/install/lib64/"
    IMPORTED_LIBRARIES
        openssl::libcrypto "${CMAKE_BINARY_DIR}/openssl/install/${OPENSSL_LIBP}/libcrypto.a"
        openssl::libssl "${CMAKE_BINARY_DIR}/openssl/install/${OPENSSL_LIBP}/libssl.a"
    BUNDLE_TARGET openssl::openssl
    BUNDLE_LIBRARIES openssl::libssl openssl::libcrypto
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/openssl
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/openssl-4.0.2
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/openssl/install
        CONFIGURE_COMMAND   /bin/sh -c
                                "export PERL5LIB=\"${CMAKE_BINARY_DIR}/perl/install/lib/5.42.0/\" PATH=\"${CMAKE_BINARY_DIR}/perl/install/bin:$PATH\" && \
                                <SOURCE_DIR>/Configure ${OPENSSL_TARGET} no-shared no-docs no-tests no-apps --prefix=<INSTALL_DIR> --openssldir=\"<INSTALL_DIR>/ssl\" ${OPENSSL_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "${OPENSSL_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${OPENSSL_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/${OPENSSL_LIBP}/libcrypto.a  <INSTALL_DIR>/${OPENSSL_LIBP}/libssl.a
        TEST_COMMAND        ""
        DEPENDS PERL
)

# ICU
set(ICU_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${ICU_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "ICU: ${ICU_MAKE_ENTIRE}")
add_cmake_external_project_from_vendored_source_tarball(ICU
    PREPARE_DIRECTORIES ${CMAKE_BINARY_DIR}/ExternalLibraries/icu4c/p

    EXPORT_DIRECTORY_VARIABLES ICU
    PKGCONFIG_VARIABLE ICU_PKGCONFIG_DIR
    MAKE_DIRECTORIES "<INSTALL_DIR>/include" "<INSTALL_DIR>/lib"
    IMPORTED_LIBRARIES
        icu4c::libicudata "<INSTALL_DIR>/lib/libicudata.a"
        icu4c::libicui18n "<INSTALL_DIR>/lib/libicui18n.a"
        icu4c::libicutest "<INSTALL_DIR>/lib/libicutest.a"
        icu4c::libicutu "<INSTALL_DIR>/lib/libicutu.a"
        icu4c::libicuuc "<INSTALL_DIR>/lib/libicuuc.a"
    BUNDLE_TARGET icu4c::icu4c
    BUNDLE_LIBRARIES icu4c::libicuuc icu4c::libicudata icu4c::libicui18n icu4c::libicutu icu4c::libicutest
    BUNDLE_DEPENDS ICU
    GLOBAL_INCLUDE_DIRECTORIES "<INSTALL_DIR>/include"
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/icu4c
        SOURCE_DIR          ${CMAKE_BINARY_DIR}/ExternalLibraries/icu4c
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/icu4c/install
        CONFIGURE_COMMAND   /bin/sh -c
                                "rm -rf <SOURCE_DIR> && \
                                cp -r ${CMAKE_SOURCE_DIR}/ExternalLibraries/icu4c-78.3/ <SOURCE_DIR> && \
                                cd <SOURCE_DIR> && \
                                export CXXFLAGS='-fPIC -std=c++17' CFLAGS='-fPIC -std=c11' && \
                                source/runConfigureICU Linux/gcc --prefix=<INSTALL_DIR> --disable-shared --enable-static --disable-tests --disable-samples ${ICU_CONFIG_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "cd <SOURCE_DIR> && ${ICU_MAKE_ENTIRE}"
        INSTALL_COMMAND     /bin/sh -c "cd <SOURCE_DIR> && ${MAKE_EXECUTABLE} install ${ICU_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libicudata.a <INSTALL_DIR>/lib/libicui18n.a <INSTALL_DIR>/lib/libicutest.a <INSTALL_DIR>/lib/libicutu.a <INSTALL_DIR>/lib/libicuuc.a
)

# libpsl

set(LIBPSL_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${LIBPSL_MAKE_ADDITIONAL_FLAGS}")
set(PSL_SRC_DEST ${CMAKE_BINARY_DIR}/ExternalLibraries/libpsl)
message(STATUS "LIBPSL: ${LIBPSL_MAKE_ENTIRE}")
add_cmake_external_project_from_vendored_source_tarball(LIBPSL
    EXPORT_DIRECTORY_VARIABLES LIBPSL
    MAKE_DIRECTORIES "<INSTALL_DIR>/include" "<INSTALL_DIR>/lib"
    IMPORTED_LIBRARIES
        libpsl::libpsl "<INSTALL_DIR>/lib/libpsl.a"
    GLOBAL_INCLUDE_DIRECTORIES "<INSTALL_DIR>/include"
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/libpsl
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/libpsl-0.23.3
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/libpsl/install
        CONFIGURE_COMMAND   /bin/sh -c
                                    "rm -rf ${PSL_SRC_DEST}/ && \
                                    mkdir -p ${PSL_SRC_DEST}/ && \
                                    cp -r <SOURCE_DIR>/* ${PSL_SRC_DEST}/ && \
                                    cd ${PSL_SRC_DEST} && \
                                    autoreconf -f && \
                                    export LIBS=-lstdc++ PKG_CONFIG_PATH=\"${ICU_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}\" && \
                                    ${PSL_SRC_DEST}/configure --prefix=<INSTALL_DIR> --enable-runtime=libicu --disable-shared --enable-static ${LIBPSL_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "cd ${PSL_SRC_DEST} && ${LIBPSL_MAKE_ENTIRE} CC=\"${MAKE_CC_EXEC}\" CFLAGS=-fPIC CXXFLAGS=-fPIC CXX=\"${MAKE_CXX_EXEC}\""
        INSTALL_COMMAND     /bin/sh -c "cd ${PSL_SRC_DEST} && ${MAKE_EXECUTABLE} install ${LIBPSL_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libpsl.a
        TEST_COMMAND        ""
        DEPENDS ICU
)

# Ncurses

set(NCURSES_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${NCURSES_MAKE_ADDITIONAL_FLAGS}")
message(STATUS "NCURSES: ${NCURSES_MAKE_ENTIRE}")
add_cmake_external_project_from_vendored_source_tarball(NCURSES
    EXPORT_DIRECTORY_VARIABLES NCURSES
    MAKE_DIRECTORIES "<INSTALL_DIR>/include" "<INSTALL_DIR>/lib"
    IMPORTED_LIBRARIES
        ncurses::ncursesw "<INSTALL_DIR>/lib/libncursesw.a"
    BUNDLE_TARGET ncurses::ncurses
    BUNDLE_LIBRARIES ncurses::ncursesw
    GLOBAL_INCLUDE_DIRECTORIES "<INSTALL_DIR>/include/ncursesw" "<INSTALL_DIR>/include"
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/ncurses
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/ncurses-6.6
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/ncurses/install
        CONFIGURE_COMMAND   <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --without-shared --without-tests --without-cxx-binding ${NCURSES_CONFIGURE_ADDITIONAL_FLAGS}
        BUILD_COMMAND       /bin/sh -c "${NCURSES_MAKE_ENTIRE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" CFLAGS=-fPIC CXXFLAGS=-fPIC"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${NCURSES_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libncursesw.a
        TEST_COMMAND        ""
)

# Readline

set(READLINE_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${READLINE_MAKE_ADDITIONAL_FLAGS} CFLAGS=-fPIC CXXFLAGS=-fPIC")
message(STATUS "READLINE: ${READLINE_MAKE_ENTIRE}")
add_cmake_external_project_from_vendored_source_tarball(READLINE
    EXPORT_DIRECTORY_VARIABLES READLINE
    MAKE_DIRECTORIES "<INSTALL_DIR>/include" "<INSTALL_DIR>/lib"
    IMPORTED_LIBRARIES
        readline::readlineas "<INSTALL_DIR>/lib/libreadline.a"
        readline::libhistory "<INSTALL_DIR>/lib/libhistory.a"
    BUNDLE_TARGET readline::readline
    BUNDLE_LIBRARIES readline::readlineas readline::libhistory
    GLOBAL_INCLUDE_DIRECTORIES "<INSTALL_DIR>/include/readline" "<INSTALL_DIR>/include"
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/readline
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/readline-8.3
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/readline/install
        CONFIGURE_COMMAND   /bin/sh -c
                                "CPPFLAGS='-DNCURSES_STATIC -I${NCURSES_INCLUDE_DIR} -I${NCURSES_INCLUDE_DIR}/ncursesw -fPIE' \
                                LDFLAGS='-L${NCURSES_LIB_DIR}' \
                                <SOURCE_DIR>/configure --prefix=<INSTALL_DIR> --with-curses ${READLINE_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "${READLINE_MAKE_ENTIRE}"
        INSTALL_COMMAND     ${MAKE_EXECUTABLE} install ${READLINE_MAKE_INSTALL_ADDITIONAL_FLAGS}
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libhistory.a <INSTALL_DIR>/lib/libreadline.a
        TEST_COMMAND        ""
        DEPENDS NCURSES
)

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
set(tar_dest "${CMAKE_BINARY_DIR}/ExternalLibraries/tar-1.35/")
add_cmake_external_project_from_vendored_source_tarball(TAR
    PREPARE_DIRECTORIES ${CMAKE_BINARY_DIR}/ExternalLibraries/tar-1.35/p
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_BINARY_DIR}/tar
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/tar-1.35
        INSTALL_DIR         ${CMAKE_BINARY_DIR}/tar/install
        CONFIGURE_COMMAND   /bin/sh -c
                                "cp -r <SOURCE_DIR>/* \"${tar_dest}\" && \
                                cd \"${tar_dest}\" && autoreconf -f && \
                                export MAKEINFO=true CC=\"${CMAKE_C_COMPILER}\" LD=\"${CMAKE_LINKER}\" CFLAGS=\"${TAR_C_FLAGS}\" LDFLAGS=\"${TAR_LD_FLAGS}\" && \
                                \"${tar_dest}\"/configure --prefix=<INSTALL_DIR> --without-gzip --without-bzip2 --without-xz --without-zstd --without-lzip --without-lzma --without-lzop --without-compress --disable-nls ${TAR_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "cd \"${tar_dest}\" && ${TAR_MAKE_ENTIRE} CFLAGS=\"${TAR_C_FLAGS}\" LDFLAGS=\"${TAR_LD_FLAGS}\""
        INSTALL_COMMAND     /bin/sh -c "cd \"${tar_dest}\" && ${MAKE_EXECUTABLE} install ${TAR_MAKE_INSTALL_ADDITIONAL_FLAGS} && STRIP=${CMAKE_STRIP} ${CMAKE_SOURCE_DIR}/script/strip.sh \"<INSTALL_DIR>/bin/tar\""
        TEST_COMMAND        <INSTALL_DIR>/bin/tar --version
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/bin/tar
)

# libmaxmind GeoIP Database
if ("${CCDB_FUCKAROUND_BUILD}" STREQUAL "True")
    set(MAXMIND_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${MAXMIND_MAKE_ADDITIONAL_FLAGS} CFLAGS=-fPIC CXXFLAGS=-fPIC")
    message(STATUS "MAXMIND: ${MAXMIND_MAKE_ENTIRE}")
    add_cmake_external_project_from_vendored_source_tarball(MAXMIND
        PREPARE_DIRECTORIES ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/p

        EXPORT_DIRECTORY_VARIABLES MAXMIND
        MAKE_DIRECTORIES "<INSTALL_DIR>/include" "<INSTALL_DIR>/lib"
        IMPORTED_LIBRARIES
            maxmind::libmaxminddb "<INSTALL_DIR>/lib/libmaxminddb.a"
        BUNDLE_TARGET maxmind::libmaxmind
        BUNDLE_LIBRARIES maxmind::libmaxminddb
        GLOBAL_INCLUDE_DIRECTORIES "<INSTALL_DIR>/include"
        EXTERNAL_PROJECT_OPTIONS
            PREFIX              ${CMAKE_BINARY_DIR}/libmaxminddb
            SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/libmaxminddb-1.13.3
            INSTALL_DIR         ${CMAKE_BINARY_DIR}/libmaxminddb/install
            CONFIGURE_COMMAND   /bin/sh -c
                                    "rm -rf ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb && \
                                    mkdir -p ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb && \
                                    cp -r <SOURCE_DIR>/* ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && \
                                    cd ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && \
                                    autoreconf -fiv && \
                                    ./configure --prefix=<INSTALL_DIR> ${MAXMIND_CONFIGURE_ADDITIONAL_FLAGS} --enable-static --disable-shared --disable-tests"
            BUILD_COMMAND       /bin/sh -c "cd ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && ${MAXMIND_MAKE_ENTIRE}"
            INSTALL_COMMAND     /bin/sh -c "cd ${CMAKE_BINARY_DIR}/ExternalLibraries/libmaxminddb/ && ${MAKE_EXECUTABLE} install ${MAXMIND_MAKE_INSTALL_ADDITIONAL_FLAGS}"
            INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libmaxminddb.a
            TEST_COMMAND        ""
    )

endif()

# libunwind
set(LIBUNWIND_MAKE_ENTIRE "${MAKE_EXECUTABLE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" ${LIBUNWIND_MAKE_ADDITIONAL_FLAGS}")
set(LIBUNWIND_SRC_DEST ${CMAKE_CURRENT_BINARY_DIR}/libunwind/src)
message(STATUS "LIBUNWIND: ${LIBUNWIND_MAKE_ENTIRE}")
add_cmake_external_project_from_vendored_source_tarball(LIBUNWIND
    EXPORT_DIRECTORY_VARIABLES LIBUNWIND
    MAKE_DIRECTORIES "<INSTALL_DIR>/include" "<INSTALL_DIR>/lib"
    IMPORTED_LIBRARIES
        libunwind::libunwind_main "<INSTALL_DIR>/lib/libunwind.a"
        libunwind::libunwind_generic "<INSTALL_DIR>/lib/libunwind-generic.a"
    BUNDLE_TARGET libunwind::libunwind
    BUNDLE_LIBRARIES libunwind::libunwind_generic libunwind::libunwind_main
    GLOBAL_INCLUDE_DIRECTORIES "<INSTALL_DIR>/include"
    EXTERNAL_PROJECT_OPTIONS
        PREFIX              ${CMAKE_CURRENT_BINARY_DIR}/libunwind
        SOURCE_DIR          ${CMAKE_SOURCE_DIR}/ExternalLibraries/libunwind-1.8.3
        INSTALL_DIR         ${CMAKE_CURRENT_BINARY_DIR}/libunwind/install
        CONFIGURE_COMMAND   /bin/sh -c "cp -r <SOURCE_DIR>/* ${LIBUNWIND_SRC_DEST} && cd ${LIBUNWIND_SRC_DEST} && CPPFLAGS='-I${ZLIB_INSTALL_DIR}/include' LDFLAGS='-L${ZLIB_INSTALL_DIR}/lib' ${LIBUNWIND_SRC_DEST}/configure --prefix=<INSTALL_DIR> --disable-tests --disable-shared --enable-static --disable-cxx-exceptions --disable-minidebuginfo ${LIBUNWIND_CONFIGURE_ADDITIONAL_FLAGS}"
        BUILD_COMMAND       /bin/sh -c "cd ${LIBUNWIND_SRC_DEST} && ${LIBUNWIND_MAKE_ENTIRE} CC=\"${MAKE_CC_EXEC}\" CXX=\"${MAKE_CXX_EXEC}\" CFLAGS=-fPIC CXXFLAGS=-fPIC"
        INSTALL_COMMAND     /bin/sh -c "cd ${LIBUNWIND_SRC_DEST} && ${MAKE_EXECUTABLE} install ${LIBUNWIND_MAKE_INSTALL_ADDITIONAL_FLAGS}"
        INSTALL_BYPRODUCTS  <INSTALL_DIR>/lib/libunwind.a <INSTALL_DIR>/lib/libunwind-generic.a <INSTALL_DIR>/lib/libunwind-ptrace.a <INSTALL_DIR>/lib/libunwind-setjmp.a <INSTALL_DIR>/lib/libunwind-coredump.a
        TEST_COMMAND        ""
)

add_dependencies(LIBUNWIND zlibstatic)
set(CACHES_BUILD_TEST OFF)
add_subdirectory(caches-0.1.1)
include_directories(caches-0.1.1/include)
