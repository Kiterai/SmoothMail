cmake_minimum_required(VERSION 3.30)
project(SmoothMailPackageTools LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC OFF)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
add_executable(smoothmail_package package.cpp)
target_link_libraries(smoothmail_package PRIVATE Qt6::Core)
if(MSVC)
    target_compile_options(smoothmail_package PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME package_guards COMMAND "${CMAKE_COMMAND}"
    "-DHELPER=$<TARGET_FILE:smoothmail_package>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_package_guards.cmake")
add_test(NAME build_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_build_guards.cmake")
add_test(NAME package_entry_guards COMMAND "${CMAKE_COMMAND}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/cmake/run_package_entry_guards.cmake")
