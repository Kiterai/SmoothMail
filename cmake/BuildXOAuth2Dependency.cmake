cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
smoothmail_fetch_dependency(cyrus-sasl-xoauth2 source)
set(binary "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/cyrus-sasl-xoauth2")
smoothmail_dependency_command("${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/native-xoauth2" -B "${binary}" -G Ninja
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${prefix}"
    "-DSASL_PREFIX=${prefix}"
    "-DXOAUTH2_SOURCE=${source}" ${privacy_options})
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${binary}" --parallel 4)
smoothmail_dependency_command("${CMAKE_COMMAND}" --install "${binary}")
