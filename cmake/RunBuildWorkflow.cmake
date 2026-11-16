cmake_minimum_required(VERSION 3.30)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT WIN32 OR NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Windows Debug/Release required.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/LicenseTemplates.cmake")
smoothmail_read_license_env("${repository}/.env")
file(SHA256 "${repository}/.env" initial_env_hash)
foreach(key IN ITEMS QT_ROOT DEPENDENCY_ROOT CRT_BIN DOWNLOAD_CURL SOURCE_ARCHIVES ASSET_CACHE)
    if(NOT SMOOTHMAIL_${key} STREQUAL "")
        get_filename_component(SMOOTHMAIL_${key} "${SMOOTHMAIL_${key}}" ABSOLUTE BASE_DIR "${repository}")
    endif()
endforeach()
if(SMOOTHMAIL_QT_ROOT STREQUAL "")
    message(FATAL_ERROR "Set SMOOTHMAIL_QT_ROOT in .env.")
endif()
if(SMOOTHMAIL_DEPENDENCY_ROOT STREQUAL "")
    set(SMOOTHMAIL_DEPENDENCY_ROOT "${repository}/build/deps")
endif()
find_program(compiler cl REQUIRED)
find_program(make nmake REQUIRED)
file(MAKE_DIRECTORY "${WORKFLOW_DIR}")
file(LOCK "${WORKFLOW_DIR}/workflow.lock" GUARD PROCESS TIMEOUT 0)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef job_id)
set(job "${WORKFLOW_DIR}/jobs/${job_id}")
file(MAKE_DIRECTORY "${job}")
set(application "${WORKFLOW_DIR}/app")
function(checked)
    execute_process(COMMAND ${ARGV} WORKING_DIRECTORY "${repository}" RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Build workflow stopped (${result}); previous outputs retained.")
    endif()
endfunction()
set(testing OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(testing ON)
else()
    find_package(Git REQUIRED)
    checked("${GIT_EXECUTABLE}" diff --quiet HEAD)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${repository}"
        OUTPUT_VARIABLE SMOOTHMAIL_SOURCE_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
endif()
set(inputs
    "-DQt6_ROOT=${SMOOTHMAIL_QT_ROOT}" "-DDEPENDENCY_ROOT=${SMOOTHMAIL_DEPENDENCY_ROOT}"
    "-DSMOOTHMAIL_SOURCE_ARCHIVES=${SMOOTHMAIL_SOURCE_ARCHIVES}"
    "-DSMOOTHMAIL_ASSET_CACHE=${SMOOTHMAIL_ASSET_CACHE}"
    "-DSMOOTHMAIL_DOWNLOAD_CURL=${SMOOTHMAIL_DOWNLOAD_CURL}")
# Compilation, dependency preparation and compiler settings stay in CMake.
checked("${CMAKE_COMMAND}" ${inputs} "-DAPPLICATION_BUILD=${application}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" "-DBUILD_TESTING=${testing}"
    -DSMOOTHMAIL_DISTRIBUTION=OFF -DSMOOTHMAIL_LEGAL_DIR=
    "-DSMOOTHMAIL_SOURCE_COMMIT=${SMOOTHMAIL_SOURCE_COMMIT}"
    -P "${CMAKE_CURRENT_LIST_DIR}/BuildWindows.cmake")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    checked("${CMAKE_COMMAND}" ${inputs} -P "${CMAKE_CURRENT_LIST_DIR}/BuildDistributionTools.cmake")
    checked("${PYTHON}" "${repository}/tools/build.py" prepare
        --directory "${WORKFLOW_DIR}" --job "${job}")
    checked("${CMAKE_COMMAND}" -S "${repository}" -B "${application}"
        -DSMOOTHMAIL_DISTRIBUTION=ON "-DSMOOTHMAIL_LEGAL_DIR=${job}/licenses")
    checked("${CMAKE_COMMAND}" --build "${application}" --parallel 4)
endif()
file(SHA256 "${repository}/.env" final_env_hash)
if(NOT initial_env_hash STREQUAL final_env_hash)
    message(FATAL_ERROR ".env changed during the build; rerun without changing inputs.")
endif()
# Runtime assembly, notices, archives and ZIP handling are Python batch work.
checked("${PYTHON}" "${repository}/tools/build.py" finish
    --directory "${WORKFLOW_DIR}" --job "${job}" --configuration "${CMAKE_BUILD_TYPE}")
