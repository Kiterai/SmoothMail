cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
if(NOT WIN32 OR NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "Windows dependencies require MSVC x64 and Debug or Release.")
endif()
find_program(msvc_compiler cl.exe REQUIRED)
find_program(nmake_tool nmake.exe REQUIRED)
# Separate configure/build/install invocations preserve each upstream project's
# assumptions and our independent pristine/patched source and receipt checks.
foreach(stage IN ITEMS Native Framework WinFlexBison I18n Solid Kio Sasl XOAuth2 Keychain Mail)
    if(stage STREQUAL XOAuth2)
        set(script BuildXOAuth2Dependency)
    elseif(stage STREQUAL WinFlexBison)
        set(script BuildWinFlexBison)
    else()
        set(script "Build${stage}Dependencies")
    endif()
    smoothmail_dependency_command("${CMAKE_COMMAND}"
        "-DQt6_ROOT=${Qt6_ROOT}" "-DDEPENDENCY_ROOT=${DEPENDENCY_ROOT}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        -P "${CMAKE_CURRENT_LIST_DIR}/${script}.cmake")
endforeach()
message(STATUS "Windows dependency prefix ready (${CMAKE_BUILD_TYPE}).")
