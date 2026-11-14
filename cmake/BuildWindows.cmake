cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/BuildWindowsDependencies.cmake")
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED APPLICATION_BUILD)
    set(APPLICATION_BUILD "${repository}/build/windows/${CMAKE_BUILD_TYPE}")
endif()
if(NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING ON)
endif()
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(native_bin "${SMOOTHMAIL_NATIVE_PREFIX}/debug/bin")
else()
    set(native_bin "${SMOOTHMAIL_NATIVE_PREFIX}/bin")
endif()
set(gettext_tools "${SMOOTHMAIL_GETTEXT_TOOLS_BIN}")
set(ENV{PATH} "${native_bin};${gettext_tools};${prefix}/bin;${Qt6_ROOT}/bin;$ENV{PATH}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${repository}" -B "${APPLICATION_BUILD}" -G Ninja
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DQt6_ROOT=${Qt6_ROOT}" "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DDEPENDENCY_ROOT=${DEPENDENCY_ROOT}" "-DSMOOTHMAIL_NATIVE_ROOT=${SMOOTHMAIL_NATIVE_ROOT}"
    "-DSMOOTHMAIL_GETTEXT_TOOLS_ROOT=${SMOOTHMAIL_GETTEXT_TOOLS_ROOT}"
    "-DBUILD_TESTING=${BUILD_TESTING}" "-DSMOOTHMAIL_DISTRIBUTION=${SMOOTHMAIL_DISTRIBUTION}"
    "-DSMOOTHMAIL_TEST_NATIVE=${SMOOTHMAIL_TEST_NATIVE}"
    "-DSMOOTHMAIL_LEGAL_DIR=${SMOOTHMAIL_LEGAL_DIR}"
    "-DSMOOTHMAIL_SOURCE_COMMIT=${SMOOTHMAIL_SOURCE_COMMIT}"
    "-DSMOOTHMAIL_SASL_PLUGIN_DIR=${prefix}/bin/sasl2"
    "-DGETTEXT_MSGFMT_EXECUTABLE=${gettext_tools}/msgfmt.exe"
    "-DGETTEXT_MSGMERGE_EXECUTABLE=${gettext_tools}/msgmerge.exe"
    RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Application configure failed.")
endif()
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${APPLICATION_BUILD}" --parallel 4)
