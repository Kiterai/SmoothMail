cmake_minimum_required(VERSION 3.30)

# Explicit opt-in bootstrap, never run automatically by application configure.
# Run from the same compiler environment used to build SmoothMail.
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PreparePatchedSource.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ModifiedDependencyReceipt.cmake")

smoothmail_build_dependency(extra-cmake-modules)
if(WIN32)
    # A globally configured CXX=g++ must never mix MinGW with the supplied
    # MSVC Qt SDK. Keep a distinct directory instead of reusing another ABI.
    find_program(msvc_compiler NAMES cl.exe REQUIRED)
    get_filename_component(msvc_tools "${msvc_compiler}" DIRECTORY)
    set(compiler_options "-DCMAKE_C_COMPILER=${msvc_compiler}" "-DCMAKE_CXX_COMPILER=${msvc_compiler}"
        "-DCMAKE_LINKER=${msvc_tools}/link.exe" "-DCMAKE_AR=${msvc_tools}/lib.exe")
endif()
foreach(dependency IN ITEMS kcodecs kmime)
    smoothmail_fetch_dependency("${dependency}" pristine)
    string(JSON commit GET "${manifest}" "${dependency}" commit)
    get_filename_component(patch "${CMAKE_CURRENT_LIST_DIR}/../patches/${dependency}/6.31.0-safe-ctype.patch" ABSOLUTE)
    smoothmail_prepare_patched_source("${dependency}" "${pristine}" "${commit}" "${patch}" "${DEPENDENCY_ROOT}/patched-src" source)
    if(WIN32)
        set(binary "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/${dependency}-safe-ctype-msvc-debug")
    else()
        set(binary "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/${dependency}-safe-ctype")
    endif()
    smoothmail_build_dependency_source("${dependency}" "${source}" "${binary}" ${compiler_options})
    set(notices "${prefix}/share/licenses/${dependency}")
    file(MAKE_DIRECTORY "${notices}")
    file(COPY "${pristine}/LICENSES" "${patch}" "${CMAKE_CURRENT_LIST_DIR}/../THIRD_PARTY_NOTICES.md" DESTINATION "${notices}")
    if(dependency STREQUAL kcodecs)
        set(artifact "${prefix}/bin/KF6Codecs.dll")
    else()
        set(artifact "${prefix}/bin/KF6Mime.dll")
    endif()
    smoothmail_write_modified_receipt("${notices}" "${artifact}" "${commit}" "${patch}")
endforeach()

message(STATUS "MIME dependency prefix: ${prefix}")
message(STATUS "KIMAP/KSMTP/SASL are NOT built by this MIME-only bootstrap.")
