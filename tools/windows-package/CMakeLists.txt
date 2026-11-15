cmake_minimum_required(VERSION 3.30)
project(SmoothMailPackageTools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC OFF)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
add_executable(smoothmail_package package.cpp)
target_link_libraries(smoothmail_package PRIVATE Qt6::Core)
add_executable(smoothmail_qt_sbom_audit qt-sbom-audit.cpp)
target_link_libraries(smoothmail_qt_sbom_audit PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(smoothmail_package PRIVATE /utf-8)
    target_compile_options(smoothmail_qt_sbom_audit PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME license_template_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_license_template_guards.cmake")
add_test(NAME source_archive_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_source_archive_guards.cmake")
add_test(NAME crt_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_crt_guards.cmake")
if(WIN32 AND DEFINED ENV{VCToolsRedistDir})
    add_test(NAME crt_binary_guards COMMAND "${CMAKE_COMMAND}"
        "-DCRT_BIN=$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_crt_binary_guards.cmake")
endif()
add_test(NAME qt_sbom_guards COMMAND "${CMAKE_COMMAND}"
    "-DHELPER=$<TARGET_FILE:smoothmail_qt_sbom_audit>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_qt_sbom_guards.cmake")
add_test(NAME package_guards COMMAND "${CMAKE_COMMAND}"
    "-DHELPER=$<TARGET_FILE:smoothmail_package>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_package_guards.cmake")
add_test(NAME build_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_build_guards.cmake")
add_test(NAME package_entry_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_package_entry_guards.cmake")
