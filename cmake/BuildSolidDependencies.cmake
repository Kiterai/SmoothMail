cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
smoothmail_build_dependency(solid
    "-DFLEX_EXECUTABLE=${DEPENDENCY_ROOT}/tools/winflexbison/win_flex.exe"
    "-DBISON_EXECUTABLE=${DEPENDENCY_ROOT}/tools/winflexbison/win_bison.exe"
    -DUSE_DBUS=OFF -DCMAKE_DISABLE_FIND_PACKAGE_IMobileDevice=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_PList=ON)
