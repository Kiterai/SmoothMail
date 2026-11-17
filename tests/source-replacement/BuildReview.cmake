cmake_minimum_required(VERSION 3.30)
# Compile ABI-compatible, visibly modified libraries from the supplied archives.
# All source edits and build products stay inside a new disposable review root.
foreach(key IN ITEMS REVIEW_ROOT Qt6_ROOT SMOOTHMAIL_SOURCE_ARCHIVES REFERENCE_PREFIX CMAKE_MAKE_PROGRAM)
    if(NOT DEFINED ${key} OR "${${key}}" STREQUAL "")
        message(FATAL_ERROR "Specify ${key}.")
    endif()
endforeach()
get_filename_component(REVIEW_ROOT "${REVIEW_ROOT}" ABSOLUTE)
if(EXISTS "${REVIEW_ROOT}" OR IS_SYMLINK "${REVIEW_ROOT}")
    message(FATAL_ERROR "Choose a new REVIEW_ROOT; existing work is never overwritten.")
endif()
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(DEPENDENCY_ROOT "${REVIEW_ROOT}/archive-inputs")
set(CMAKE_BUILD_TYPE Release)
set(ENV{GIT_CEILING_DIRECTORIES} "${REVIEW_ROOT}")
include("${repository}/cmake/DependencyBuild.cmake")
include("${repository}/cmake/SourceArchives.cmake")
include("${repository}/cmake/QtSourceSettings.cmake")
smoothmail_check_qt_review_output("${Qt6_ROOT}" "${REVIEW_ROOT}")
smoothmail_import_source_archive(qtsvg 883a58532078d003cefef0a549bfe9f885ce41f5 svg_source)
string(JSON codecs_commit GET "${manifest}" kcodecs commit)
smoothmail_import_source_archive(kcodecs "${codecs_commit}" codecs_source)
file(COPY "${svg_source}/" DESTINATION "${REVIEW_ROOT}/src/qtsvg")
smoothmail_prepare_archive_patch(kcodecs "${codecs_source}" "${codecs_commit}"
    "${repository}/patches/kcodecs/6.31.0-safe-ctype.patch" "${REVIEW_ROOT}/patched/kcodecs")
foreach(id IN ITEMS qtsvg kcodecs)
    if(id STREQUAL qtsvg)
        set(base "${REVIEW_ROOT}/src/qtsvg")
        set(subdir src/svg)
        set(target Svg)
        set(symbol smoothmail_source_replacement_svg)
    else()
        set(base "${REVIEW_ROOT}/patched/kcodecs")
        set(subdir src)
        set(target KF6Codecs)
        set(symbol smoothmail_source_replacement_codecs)
    endif()
    set(modified "${REVIEW_ROOT}/modified/src/${id}")
    file(COPY "${base}/" DESTINATION "${modified}")
    file(WRITE "${modified}/${subdir}/replacement-marker.cpp"
        "extern \"C\" __declspec(dllexport) int ${symbol}() { return 45654; }\n")
    file(APPEND "${modified}/${subdir}/CMakeLists.txt"
        "\ntarget_sources(${target} PRIVATE replacement-marker.cpp)\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${modified}"
        -B "${REVIEW_ROOT}/modified/build/${id}" -G Ninja
        "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" -DCMAKE_BUILD_TYPE=Release
        "-DCMAKE_INSTALL_PREFIX=${REVIEW_ROOT}/modified/install"
        "-DQt6_ROOT=${Qt6_ROOT}" "-DCMAKE_PREFIX_PATH=${REFERENCE_PREFIX};${Qt6_ROOT}"
        -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_DOC=OFF -DBUILD_QCH=OFF
        -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF -DQT_BUILD_TESTS=OFF
        -DQT_BUILD_EXAMPLES=OFF -DQT_GENERATE_SBOM=OFF
        COMMAND_ERROR_IS_FATAL ANY)
    smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${REVIEW_ROOT}/modified/build/${id}" --parallel 4)
    smoothmail_dependency_command("${CMAKE_COMMAND}" --install "${REVIEW_ROOT}/modified/build/${id}")
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}"
    -B "${REVIEW_ROOT}/probe" -G Ninja "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    -DCMAKE_BUILD_TYPE=Release "-DQt6_ROOT=${Qt6_ROOT}"
    "-DCMAKE_PREFIX_PATH=${REFERENCE_PREFIX};${Qt6_ROOT}" COMMAND_ERROR_IS_FATAL ANY)
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${REVIEW_ROOT}/probe" --parallel 4)
message(STATUS "Modified QtSvg/KCodecs and the shared baseline/replacement probe built in ${REVIEW_ROOT}")
