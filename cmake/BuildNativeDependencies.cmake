cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
if(NOT WIN32)
    message(FATAL_ERROR "This native bootstrap currently supports Windows x64 only.")
endif()
smoothmail_fetch_dependency(vcpkg-recipes source)
file(READ "${CMAKE_CURRENT_LIST_DIR}/native-dependencies.json" native_manifest)
string(JSON tool_url GET "${native_manifest}" vcpkgTool url)
string(JSON tool_hash GET "${native_manifest}" vcpkgTool sha512)
set(tool "${source}/vcpkg.exe")
smoothmail_download_asset("${tool_url}" SHA512 "${tool_hash}" "${tool}")

set(ENV{VCPKG_DISABLE_METRICS} 1)
set(ENV{VCPKG_BINARY_SOURCES} clear)
if(DEFINED ENV{VSINSTALLDIR})
    # vcvars appends a separator, but vcpkg compares this to discovery paths.
    string(REGEX REPLACE "[/\\\\]+$" "" vs_install "$ENV{VSINSTALLDIR}")
    set(ENV{VCPKG_VISUAL_STUDIO_PATH} "${vs_install}")
endif()
# Route vcpkg assets through the same verified CMake downloader. Do not fall
# back to an external curl executable or an unverified origin download.
set(ENV{X_VCPKG_ASSET_SOURCES}
    "clear;x-script,\"${CMAKE_COMMAND}\" \"-DURL={url}\" \"-DSHA512={sha512}\" \"-DDESTINATION={dst}\" -P \"${CMAKE_CURRENT_LIST_DIR}/FetchVcpkgAsset.cmake\";x-block-origin")
# No Qt port or toolchain file is involved. Native C dependencies and host
# gettext tools live in a separate prefix from the developer's Qt SDK.
smoothmail_dependency_command("${tool}" install "gettext[tools]:x64-windows"
    --classic "--x-install-root=${DEPENDENCY_ROOT}/native")
