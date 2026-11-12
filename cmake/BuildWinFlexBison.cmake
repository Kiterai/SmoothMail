cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
smoothmail_fetch_dependency(winflexbison source)
set(binary "${DEPENDENCY_ROOT}/build/tools/winflexbison")
smoothmail_dependency_command("${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}" -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=${DEPENDENCY_ROOT}/tools/winflexbison")
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${binary}" --parallel 4)
smoothmail_dependency_command("${CMAKE_COMMAND}" --install "${binary}")
