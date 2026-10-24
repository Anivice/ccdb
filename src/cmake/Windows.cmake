# Native MSVC build: no Unix build tools or optional Linux dependency stack.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_subdirectory(ExternalLibraries/readline-win32)

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

# Favor execution speed in Release, including across the Readline library
# boundary. Keep /Ob3 after /O2, which otherwise selects /Ob2. Configuration
# expressions also support Release in multi-configuration generators.
foreach(target IN ITEMS ccdb ccdb_readline ccdb_embed)
    target_compile_options(${target} PRIVATE
        "$<$<CONFIG:Release>:/O2;/Ob3;/Ot;/Oi;/GL;/Gy;/Gw;/GF>")
endforeach()
foreach(target IN ITEMS ccdb ccdb_embed)
    target_link_options(${target} PRIVATE
        "$<$<CONFIG:Release>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF>")
endforeach()

install(TARGETS ccdb RUNTIME DESTINATION bin)
