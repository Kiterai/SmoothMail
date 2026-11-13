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
    set(native_bin "${DEPENDENCY_ROOT}/native/x64-windows/debug/bin")
else()
    set(native_bin "${DEPENDENCY_ROOT}/native/x64-windows/bin")
endif()
set(gettext_tools "${DEPENDENCY_ROOT}/native/x64-windows/tools/gettext/bin")
set(ENV{PATH} "${native_bin};${gettext_tools};${prefix}/bin;${Qt6_ROOT}/bin;$ENV{PATH}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${repository}" -B "${APPLICATION_BUILD}" -G Ninja
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DQt6_ROOT=${Qt6_ROOT}" "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DBUILD_TESTING=${BUILD_TESTING}" "-DSMOOTHMAIL_DISTRIBUTION=${SMOOTHMAIL_DISTRIBUTION}"
    "-DSMOOTHMAIL_SASL_PLUGIN_DIR=${prefix}/bin/sasl2"
    "-DGETTEXT_MSGFMT_EXECUTABLE=${gettext_tools}/msgfmt.exe"
    "-DGETTEXT_MSGMERGE_EXECUTABLE=${gettext_tools}/msgmerge.exe"
    RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Application configure failed.")
endif()
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${APPLICATION_BUILD}" --parallel 4)
