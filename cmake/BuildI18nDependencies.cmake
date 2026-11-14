cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
if(NOT WIN32)
    message(FATAL_ERROR "This stage currently supports the Windows native prefix only.")
endif()
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(native_library_dir "${SMOOTHMAIL_NATIVE_PREFIX}/debug/lib")
    set(native_runtime_dir "${SMOOTHMAIL_NATIVE_PREFIX}/debug/bin")
else()
    set(native_library_dir "${SMOOTHMAIL_NATIVE_PREFIX}/lib")
    set(native_runtime_dir "${SMOOTHMAIL_NATIVE_PREFIX}/bin")
endif()
set(gettext_tools "${SMOOTHMAIL_GETTEXT_TOOLS_BIN}")
foreach(required IN ITEMS "${native_library_dir}/intl.lib"
        "${SMOOTHMAIL_NATIVE_PREFIX}/include/libintl.h"
        "${gettext_tools}/msgfmt.exe" "${gettext_tools}/msgmerge.exe")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing ${required}; run BuildNativeDependencies.cmake first.")
    endif()
endforeach()
# Build-time helpers may load Qt, KI18n, and the matching native C runtime DLLs.
set(ENV{PATH} "${native_runtime_dir};${gettext_tools};${prefix}/bin;${Qt6_ROOT}/bin;$ENV{PATH}")
set(gettext_options
    "-DGETTEXT_MSGFMT_EXECUTABLE=${gettext_tools}/msgfmt.exe"
    "-DGETTEXT_MSGMERGE_EXECUTABLE=${gettext_tools}/msgmerge.exe")
smoothmail_build_dependency(ki18n -DBUILD_WITH_QML=OFF
    "-DLibIntl_INCLUDE_DIRS=${SMOOTHMAIL_NATIVE_PREFIX}/include"
    "-DLibIntl_LIBRARIES=${native_library_dir}/intl.lib"
    ${gettext_options})
smoothmail_build_dependency(kservice ${gettext_options})
message(STATUS "KI18n/KService built in ${prefix}; KIO remains a separate license gate.")
