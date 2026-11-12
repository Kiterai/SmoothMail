cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
if(NOT WIN32)
    message(FATAL_ERROR "This native bootstrap currently supports Windows x64 only.")
endif()
find_program(CURL_EXECUTABLE curl REQUIRED)
smoothmail_fetch_dependency(vcpkg-recipes source)
file(READ "${CMAKE_CURRENT_LIST_DIR}/native-dependencies.json" native_manifest)
string(JSON tool_url GET "${native_manifest}" vcpkgTool url)
string(JSON tool_hash GET "${native_manifest}" vcpkgTool sha512)
set(tool "${source}/vcpkg.exe")
if(NOT EXISTS "${tool}")
    smoothmail_dependency_command("${CURL_EXECUTABLE}" --fail --location --proto =https --proto-redir =https
        --output "${tool}" "${tool_url}")
endif()
file(SHA512 "${tool}" actual)
if(NOT actual STREQUAL tool_hash)
    message(FATAL_ERROR "vcpkg executable checksum mismatch.")
endif()

set(ENV{VCPKG_DISABLE_METRICS} 1)
set(ENV{VCPKG_BINARY_SOURCES} clear)
if(DEFINED ENV{VSINSTALLDIR})
    # vcvars appends a separator, but vcpkg compares this to discovery paths.
    string(REGEX REPLACE "[/\\\\]+$" "" vs_install "$ENV{VSINSTALLDIR}")
    set(ENV{VCPKG_VISUAL_STUDIO_PATH} "${vs_install}")
endif()
if(SMOOTHMAIL_USE_ASSET_DOWNLOADER)
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    set(ENV{X_VCPKG_ASSET_SOURCES}
        "x-script,\"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_LIST_DIR}/../tools/fetch-vcpkg-asset.py\" \"${CURL_EXECUTABLE}\" {url} {sha512} {dst}")
endif()
# No Qt port or toolchain file is involved. Native C dependencies and host
# gettext tools live in a separate prefix from the developer's Qt SDK.
smoothmail_dependency_command("${tool}" install "gettext[tools]:x64-windows"
    --classic "--x-install-root=${DEPENDENCY_ROOT}/native")
