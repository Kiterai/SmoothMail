cmake_minimum_required(VERSION 3.30)
# Opt-in real Git/download/compiler test in an independent dependency root.
# Run in an x64 MSVC development shell with Qt6_ROOT and a new DEPENDENCY_ROOT.
if(NOT DEPENDENCY_ROOT OR EXISTS "${DEPENDENCY_ROOT}")
    message(FATAL_ERROR "Specify a new independent DEPENDENCY_ROOT.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/DependencyBuild.cmake")
# DependencyBuild has already built the pinned Ninja from source with MSVC.
smoothmail_fetch_dependency(vcpkg-recipes source)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../cmake/native-dependencies.json" native)
string(JSON url GET "${native}" vcpkgTool url)
string(JSON hash GET "${native}" vcpkgTool sha512)
set(tool "${DEPENDENCY_ROOT}/vcpkg.exe")
smoothmail_download_asset("${url}" SHA512 "${hash}" "${tool}")
set(ENV{VCPKG_DISABLE_METRICS} 1)
set(ENV{VCPKG_BINARY_SOURCES} clear)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/VcpkgAssetTransport.cmake")
smoothmail_configure_vcpkg_asset_transport()
smoothmail_dependency_command("${tool}" "--vcpkg-root=${source}"
    "--downloads-root=${DEPENDENCY_ROOT}/downloads" fetch cmake)
message(STATUS "Sandbox: pinned Ninja build and vcpkg nested CMake/tool download succeeded.")
