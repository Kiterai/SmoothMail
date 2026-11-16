cmake_minimum_required(VERSION 3.30)
# Compile audit tools only. Packaging and document generation belong to Python.
set(CMAKE_BUILD_TYPE Release)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(binary "${DEPENDENCY_ROOT}/build/tools/package")
smoothmail_dependency_command("${CMAKE_COMMAND}" -S "${repository}/tools/windows-package"
    -B "${binary}" -G Ninja "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DQt6_ROOT=${Qt6_ROOT}" -DCMAKE_BUILD_TYPE=Release)
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${binary}" --parallel 4)
include("${CMAKE_CURRENT_LIST_DIR}/WindowsCrt.cmake")
smoothmail_prepare_crt_verifier(verifier)
file(WRITE "${binary}/crt-verifier.txt" "${verifier}")
