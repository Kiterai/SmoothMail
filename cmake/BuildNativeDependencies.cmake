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
set(vcpkg_options)
if(SMOOTHMAIL_SOURCE_ARCHIVES)
    # Keep reviewed source immutable. vcpkg's downloaded tools, source builds
    # and packages belong in independently mutable working/cache directories.
    set(tool "${DEPENDENCY_ROOT}/tools/vcpkg/vcpkg.exe")
    set(downloads "${DEPENDENCY_ROOT}/downloads/vcpkg")
    set(vcpkg_options "--vcpkg-root=${source}" "--downloads-root=${downloads}"
        "--x-buildtrees-root=${DEPENDENCY_ROOT}/build/vcpkg/buildtrees"
        "--x-packages-root=${DEPENDENCY_ROOT}/build/vcpkg/packages")
    file(MAKE_DIRECTORY "${downloads}")
    foreach(cache IN ITEMS "${SMOOTHMAIL_SOURCE_ARCHIVES}" "${SMOOTHMAIL_ASSET_CACHE}")
        if(cache STREQUAL "")
            continue()
        endif()
        file(GLOB assets LIST_DIRECTORIES FALSE "${cache}/*")
        foreach(asset IN LISTS assets)
            if(IS_SYMLINK "${asset}")
                message(FATAL_ERROR "Linked source/tool cache asset.")
            endif()
            get_filename_component(name "${asset}" NAME)
            if(name MATCHES "\\.(tar\\.gz|zip|7z|exe|pkg\\.tar\\.zst)$")
                if(NOT EXISTS "${downloads}/${name}")
                    file(COPY_FILE "${asset}" "${downloads}/${name}")
                else()
                    file(SHA256 "${asset}" expected)
                    file(SHA256 "${downloads}/${name}" actual)
                    if(NOT expected STREQUAL actual)
                        message(FATAL_ERROR "Existing native download differs; nothing was overwritten.")
                    endif()
                endif()
            endif()
        endforeach()
    endforeach()
endif()
smoothmail_download_asset("${tool_url}" SHA512 "${tool_hash}" "${tool}")
file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/native-triplets/x64-windows.cmake" triplet_hash)
string(JSON recipe_commit GET "${manifest}" vcpkg-recipes commit)
string(SHA256 profile_hash "${triplet_hash};${recipe_commit};${tool_hash}")
smoothmail_prepare_native_profile("${SMOOTHMAIL_NATIVE_ROOT}" "${profile_hash}")

set(ENV{VCPKG_DISABLE_METRICS} 1)
set(ENV{VCPKG_BINARY_SOURCES} clear)
if(DEFINED ENV{VSINSTALLDIR})
    # vcvars appends a separator, but vcpkg compares this to discovery paths.
    string(REGEX REPLACE "[/\\\\]+$" "" vs_install "$ENV{VSINSTALLDIR}")
    set(ENV{VCPKG_VISUAL_STUDIO_PATH} "${vs_install}")
endif()
# Route vcpkg assets through the same verified downloader and explicit TLS
# transport. Never fall back to vcpkg's origin download or unverified bytes.
include("${CMAKE_CURRENT_LIST_DIR}/VcpkgAssetTransport.cmake")
smoothmail_configure_vcpkg_asset_transport()
# No Qt port or toolchain file is involved. Native C dependencies and host
# gettext tools live in a separate prefix from the developer's Qt SDK.
smoothmail_dependency_command("${tool}" install "gettext[tools]:x64-windows"
    --classic ${vcpkg_options} "--x-install-root=${SMOOTHMAIL_GETTEXT_TOOLS_ROOT}")
smoothmail_dependency_command("${tool}" install "gettext-libintl:x64-windows"
    --classic ${vcpkg_options} "--overlay-triplets=${CMAKE_CURRENT_LIST_DIR}/native-triplets"
    "--x-install-root=${SMOOTHMAIL_NATIVE_ROOT}")

# Audit native Release DLLs independently of the rest of the eventual runtime.
# The helper uses only the developer-supplied QtCore and never runs Python.
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(helper_build "${DEPENDENCY_ROOT}/build/tools/package")
smoothmail_dependency_command("${CMAKE_COMMAND}" -S "${repository}/tools/windows-package"
    -B "${helper_build}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DQt6_ROOT=${Qt6_ROOT}" -DCMAKE_BUILD_TYPE=Release)
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${helper_build}" --parallel 4)
set(saved_path "$ENV{PATH}")
set(ENV{PATH} "${Qt6_ROOT}/bin;$ENV{PATH}")
smoothmail_dependency_command("${helper_build}/smoothmail_package.exe" --audit-stage "${SMOOTHMAIL_NATIVE_PREFIX}/bin"
    "${repository}" "${DEPENDENCY_ROOT}" "${SMOOTHMAIL_NATIVE_ROOT}" "${Qt6_ROOT}" "$ENV{USERPROFILE}" "$ENV{TEMP}")
set(probe_build "${DEPENDENCY_ROOT}/build/tools/native-runtime-probe")
smoothmail_dependency_command("${CMAKE_COMMAND}" -S "${repository}/tests/native-runtime"
    -B "${probe_build}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DQt6_ROOT=${Qt6_ROOT}" -DCMAKE_BUILD_TYPE=Release "-DNATIVE_BIN=${SMOOTHMAIL_NATIVE_PREFIX}/bin")
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${probe_build}" --parallel 4)
smoothmail_dependency_command("${CMAKE_COMMAND}" "-DPROBE=${probe_build}/native_runtime_probe.exe"
    "-DNATIVE_BIN=${SMOOTHMAIL_NATIVE_PREFIX}/bin" -P "${repository}/tests/cmake/run_native_runtime.cmake")
set(ENV{PATH} "${saved_path}")
