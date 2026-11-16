cmake_minimum_required(VERSION 3.30)
if(NOT WIN32 OR NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Use the debug or release preset on Windows.")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
add_custom_target(smoothmail ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DWORKFLOW_DIR=${CMAKE_BINARY_DIR}" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DPYTHON=${Python3_EXECUTABLE}" -P "${CMAKE_SOURCE_DIR}/cmake/RunBuildWorkflow.cmake"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    USES_TERMINAL VERBATIM)
