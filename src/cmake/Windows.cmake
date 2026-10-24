# Native MSVC build: no Unix build tools or optional Linux dependency stack.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(ExternalLibraries/readline-win32)

# Dependency availability only; CCDB's SSL feature guards remain unchanged.
option(CCDB_WINDOWS_OPENSSL "Build native OpenSSL 4.0.3 and its C++ smoke test" OFF)
if(CCDB_WINDOWS_OPENSSL)
    enable_testing()
    add_subdirectory(ExternalLibraries/openssl-windows)
endif()

add_executable(ccdb_embed compile_utils/embed.cpp)
target_include_directories(ccdb_embed PRIVATE include)
target_compile_options(ccdb_embed PRIVATE /utf-8 /EHsc)

set(resource_sources)
function(ccdb_resource name input)
    set(base "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    add_custom_command(OUTPUT "${base}.cpp" "${base}.h"
        COMMAND ccdb_embed "${input}" "${base}" "${name}"
        DEPENDS ccdb_embed "${input}" VERBATIM)
    set(resource_sources ${resource_sources} "${base}.cpp" PARENT_SCOPE)
endfunction()
ccdb_resource(command_readline "${CMAKE_CURRENT_SOURCE_DIR}/windows/command.readline")
ccdb_resource(LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE")
ccdb_resource(ccdbrc "${CMAKE_CURRENT_SOURCE_DIR}/windows/ccdbrc.example")
configure_file(windows/command.readline.h.in command.readline.h COPYONLY)

add_executable(ccdb
    windows/main.cpp windows/platform.cpp windows/color.cpp
    misc/Readline.cpp misc/config.cpp misc/args.cpp misc/mihomo.cpp
    misc/utils.string.cpp misc/utils.time.cpp misc/utils.bin.cpp misc/sort.cpp
    ccdb/ccdb.cpp ccdb/ccdb.misc.cpp ccdb/ccdb.input.cpp
    ccdb/ccdb.get.cpp ccdb/ccdb.set.cpp
    ccdb/ccdb.get_connections.cpp ccdb/ccdb.get_log.cpp ccdb/ccdb.get_rules.cpp
    ccdb/ccdb.nload.cpp ccdb/ccdb.print_table.cpp ccdb/ccdb.continuous_table.cpp
    ccdb/general_info_pulling.cpp ${resource_sources})
target_include_directories(ccdb PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(ccdb PRIVATE
    __WINDOWS_MSVC_BUILD__=1 NOMINMAX WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS
    CCDB_VERSION="${CCDB_VERSION}")
target_compile_options(ccdb PRIVATE /utf-8 /EHsc /bigobj /Zc:__cplusplus)
target_link_libraries(ccdb PRIVATE ccdb_readline ws2_32 bcrypt psapi)

# CPU-specific code generation is opt-in: distributed binaries must continue to
# run on baseline x64 CPUs. Apply the same settings to C Readline and C++ code.
set(CCDB_WINDOWS_ARCH "SSE2" CACHE STRING "Instruction set for optimized Windows builds")
set_property(CACHE CCDB_WINDOWS_ARCH PROPERTY STRINGS SSE2 AVX AVX2 AVX512)
if(NOT CCDB_WINDOWS_ARCH MATCHES "^(SSE2|AVX|AVX2|AVX512)$")
    message(FATAL_ERROR "CCDB_WINDOWS_ARCH must be SSE2, AVX, AVX2, or AVX512")
endif()
set(CCDB_WINDOWS_FAVOR "blend" CACHE STRING "CPU tuning for optimized Windows builds")
set_property(CACHE CCDB_WINDOWS_FAVOR PROPERTY STRINGS blend AMD64 INTEL64)
if(NOT CCDB_WINDOWS_FAVOR MATCHES "^(blend|AMD64|INTEL64)$")
    message(FATAL_ERROR "CCDB_WINDOWS_FAVOR must be blend, AMD64, or INTEL64")
endif()

# Favor execution speed, including across the Readline library boundary, in
# Release and RelWithDebInfo. Keep /Ob3 after /O2, which otherwise selects /Ob2.
# Debug remains unoptimized and MinSizeRel retains its size-oriented defaults.
set(ccdb_speed_config "$<CONFIG:Release,RelWithDebInfo>")
foreach(target IN ITEMS ccdb ccdb_readline ccdb_embed)
    target_compile_options(${target} PRIVATE
        "$<${ccdb_speed_config}:/O2;/Ob3;/Ot;/Oi;/GL;/Gy;/Gw;/GF>"
        "$<${ccdb_speed_config}:/arch:${CCDB_WINDOWS_ARCH};/favor:${CCDB_WINDOWS_FAVOR}>")
endforeach()
foreach(target IN ITEMS ccdb ccdb_embed)
    target_link_options(${target} PRIVATE
        "$<${ccdb_speed_config}:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>")
endforeach()

install(TARGETS ccdb RUNTIME DESTINATION bin)
