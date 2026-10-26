cmake_minimum_required(VERSION 3.26)
# Also usable as a standalone dependency/consumer validation project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CCDBOpenSSL LANGUAGES C CXX)
    enable_testing()
endif()

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   NOT CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    message(FATAL_ERROR "CCDB OpenSSL requires native x64 MSVC")
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    message(FATAL_ERROR "Set CMAKE_BUILD_TYPE (for example Debug or Release)")
endif()

find_program(CCDB_OPENSSL_PERL NAMES perl HINTS "C:/Windows/System32/" "C:/Strawberry/perl/bin" "${CMAKE_SOURCE_DIR}/ExternalLibraries/openssl-windows/perl/perl/bin"
    DOC "Native Windows Perl executable (not MSYS/Cygwin Perl)" REQUIRED)
execute_process(COMMAND "${CCDB_OPENSSL_PERL}" -e
    "use 5.010; BEGIN { die qq(Native Windows Perl required\\n) unless $^O eq 'MSWin32'; } use File::Spec; use IPC::Cmd; print $^V;"
    RESULT_VARIABLE perl_result OUTPUT_VARIABLE perl_version ERROR_VARIABLE perl_error)
if(NOT perl_result EQUAL 0)
    message(FATAL_ERROR "OpenSSL needs native Windows Perl: ${perl_error}")
endif()
message(STATUS "OpenSSL build Perl: ${CCDB_OPENSSL_PERL} (${perl_version})")
get_filename_component(compiler_dir "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(CCDB_OPENSSL_NMAKE NAMES nmake HINTS "${compiler_dir}" REQUIRED)

# NMake cannot parallelize individual OpenSSL compile commands. The outer
# CMake/Ninja --parallel setting does not reach this ExternalProject either.
include(ProcessorCount)
ProcessorCount(openssl_default_jobs)
if(NOT openssl_default_jobs)
    set(openssl_default_jobs 1)
endif()
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND
   NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(openssl_default_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
endif()
set(CCDB_OPENSSL_JOBS "${openssl_default_jobs}" CACHE STRING
    "Parallel OpenSSL build jobs (1 uses NMake; greater than 1 requires jom)")
if(NOT CCDB_OPENSSL_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "CCDB_OPENSSL_JOBS must be a positive integer")
endif()
if(CCDB_OPENSSL_JOBS GREATER 1)
    find_program(CCDB_OPENSSL_JOM NAMES jom
        HINTS "${CMAKE_CURRENT_LIST_DIR}/tools/jom"
        DOC "Native Windows jom executable for parallel OpenSSL builds")
    if(NOT CCDB_OPENSSL_JOM)
        message(FATAL_ERROR
            "Parallel OpenSSL builds require Qt jom. Put jom.exe on PATH or set "
            "CCDB_OPENSSL_JOM to its path (see ExternalLibraries/openssl-windows/README.md). "
            "Set CCDB_OPENSSL_JOBS=1 to use serial NMake.")
    endif()
    set(openssl_build_command "${CCDB_OPENSSL_JOM}" /nologo -j "${CCDB_OPENSSL_JOBS}")
else()
    set(openssl_build_command "${CCDB_OPENSSL_NMAKE}" /nologo)
endif()
message(STATUS "OpenSSL build: ${openssl_build_command} (${CCDB_OPENSSL_JOBS} jobs)")

set(openssl_source "${CMAKE_CURRENT_LIST_DIR}/../openssl-4.0.3")
file(STRINGS "${openssl_source}/VERSION.dat" version_fields
    REGEX "^(MAJOR|MINOR|PATCH|PRE_RELEASE_TAG)=")
if(NOT "${version_fields}" STREQUAL "MAJOR=4;MINOR=0;PATCH=3;PRE_RELEASE_TAG=")
    message(FATAL_ERROR "Expected the vendored release OpenSSL 4.0.3")
endif()

# Keep native paths short enough for NMake/MSVC without requiring long-path policy.
set(openssl_root "${CMAKE_BINARY_DIR}/openssl-native")
set(openssl_install "${openssl_root}/$<CONFIG>/install")
set(openssl_runtime "${CMAKE_MSVC_RUNTIME_LIBRARY}")
if(NOT openssl_runtime)
    set(openssl_runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(ExternalProject)
ExternalProject_Add(ccdb_openssl_build
    PREFIX "${openssl_root}/ep"
    SOURCE_DIR "${openssl_source}"
    # ExternalProject creates these at configure time, before expanding $<CONFIG>.
    # Commands below select the actual configuration-specific working directory.
    BINARY_DIR "${openssl_root}/work"
    INSTALL_DIR "${openssl_root}/install"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND "${CMAKE_COMMAND}"
        "-DPERL=${CCDB_OPENSSL_PERL}" "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DBUILD_DIR=${openssl_root}/$<CONFIG>/build"
        "-DINSTALL_DIR=${openssl_install}" "-DCONFIG=$<CONFIG>"
        "-DRUNTIME=${openssl_runtime}"
        -P "${CMAKE_CURRENT_LIST_DIR}/configure.cmake"
    BUILD_COMMAND "${CMAKE_COMMAND}" -E chdir "${openssl_root}/$<CONFIG>/build"
        ${openssl_build_command} build_libs
    BUILD_ALWAYS ON
    INSTALL_COMMAND "${CMAKE_COMMAND}" -E chdir "${openssl_root}/$<CONFIG>/build"
        "${CCDB_OPENSSL_NMAKE}" /nologo install_dev
    INSTALL_BYPRODUCTS "${openssl_install}/lib/libcrypto.lib" "${openssl_install}/lib/libssl.lib"
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON LOG_OUTPUT_ON_FAILURE ON
)
# Let the native make tool track source/header changes even when stamps exist.
# Configuration-specific directories prevent Debug/Release archive mixing.
set_property(TARGET ccdb_openssl_build PROPERTY FOLDER Dependencies)
ExternalProject_Add_StepDependencies(ccdb_openssl_build configure
    "${CMAKE_CURRENT_LIST_DIR}/configure.cmake"
    "${CMAKE_CURRENT_LIST_DIR}/ccdb-msvc.conf.in"
    "${openssl_source}/Configure" "${openssl_source}/VERSION.dat")

add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
set(openssl_configs ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
list(REMOVE_DUPLICATES openssl_configs)
foreach(config IN LISTS openssl_configs)
    string(TOUPPER "${config}" config_upper)
    set(prefix "${openssl_root}/${config}/install")
    file(MAKE_DIRECTORY "${prefix}/include")
    foreach(pair IN ITEMS "Crypto|libcrypto" "SSL|libssl")
        string(REPLACE "|" ";" pair "${pair}")
        list(GET pair 0 target)
        list(GET pair 1 library)
        set_property(TARGET OpenSSL::${target} APPEND PROPERTY IMPORTED_CONFIGURATIONS "${config}")
        set_target_properties(OpenSSL::${target} PROPERTIES
            IMPORTED_LOCATION_${config_upper} "${prefix}/lib/${library}.lib")
    endforeach()
endforeach()
foreach(target IN ITEMS OpenSSL::Crypto OpenSSL::SSL)
    set_target_properties(${target} PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${openssl_install}/include")
    add_dependencies(${target} ccdb_openssl_build)
endforeach()
set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_LINK_LIBRARIES "ws2_32;gdi32;advapi32;crypt32;user32")
set_target_properties(OpenSSL::SSL PROPERTIES INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
add_library(openssl::libcrypto ALIAS OpenSSL::Crypto)
add_library(openssl::libssl ALIAS OpenSSL::SSL)
add_library(openssl::openssl INTERFACE IMPORTED GLOBAL)
target_link_libraries(openssl::openssl INTERFACE OpenSSL::SSL)
