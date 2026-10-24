if(RUNTIME STREQUAL "MultiThreadedDLL")
    set(crt_flag /MD)
elseif(RUNTIME STREQUAL "MultiThreadedDebugDLL")
    set(crt_flag /MDd)
elseif(RUNTIME STREQUAL "MultiThreaded")
    set(crt_flag /MT)
elseif(RUNTIME STREQUAL "MultiThreadedDebug")
    set(crt_flag /MTd)
else()
    message(FATAL_ERROR "Unsupported MSVC runtime: ${RUNTIME}")
endif()
if(CONFIG STREQUAL "Debug")
    set(mode --debug)
else()
    set(mode --release)
endif()
file(MAKE_DIRECTORY "${BUILD_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/ccdb-msvc.conf.in"
    "${BUILD_DIR}/ccdb-msvc.conf" @ONLY)
execute_process(COMMAND "${PERL}" "${SOURCE_DIR}/Configure"
    "--config=${BUILD_DIR}/ccdb-msvc.conf"
    CCDB-VC-WIN64A ${mode} no-shared no-asm no-apps no-docs no-tests no-module
    "--prefix=${INSTALL_DIR}" "--openssldir=${INSTALL_DIR}/ssl" --libdir=lib
    WORKING_DIRECTORY "${BUILD_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
