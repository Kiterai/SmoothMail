cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
smoothmail_build_dependency(qtkeychain -DBUILD_WITH_QT6=ON -DBUILD_TRANSLATIONS=OFF -DBUILD_TEST_APPLICATION=OFF)
smoothmail_fetch_dependency(qtkeychain source)
file(MAKE_DIRECTORY "${prefix}/share/licenses/qtkeychain")
file(COPY "${source}/COPYING"
    "${source}/qtkeychain/keychain.h"
    DESTINATION "${prefix}/share/licenses/qtkeychain")
