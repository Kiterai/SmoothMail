cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PrepareKioSource.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ModifiedDependencyReceipt.cmake")
if(NOT WIN32)
    message(FATAL_ERROR "This KIO bootstrap is currently validated for Windows only.")
endif()
smoothmail_fetch_dependency(kio pristine)
string(JSON commit GET "${manifest}" kio commit)
get_filename_component(patch "${CMAKE_CURRENT_LIST_DIR}/../patches/kio/6.31.0-disable-favicon-cache.patch" ABSOLUTE)
smoothmail_prepare_kio_source("${pristine}" "${commit}" "${patch}" "${DEPENDENCY_ROOT}/patched-src" source)
set(binary "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/kio-no-favicon")
set(native "${DEPENDENCY_ROOT}/native/x64-windows")
set(gettext_tools "${native}/tools/gettext/bin")
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(native_bin "${native}/debug/bin")
else()
    set(native_bin "${native}/bin")
endif()
set(ENV{PATH} "${native_bin};${gettext_tools};${prefix}/bin;${Qt6_ROOT}/bin;$ENV{PATH}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${prefix}"
    "-DCMAKE_PREFIX_PATH=${prefix};${Qt6_ROOT}"
    "-DQt6_ROOT=${Qt6_ROOT}"
    "-DGETTEXT_MSGFMT_EXECUTABLE=${gettext_tools}/msgfmt.exe"
    "-DGETTEXT_MSGMERGE_EXECUTABLE=${gettext_tools}/msgmerge.exe"
    -DKIOCORE_ONLY=ON -DBUILD_TESTING=OFF -DBUILD_SHARED_LIBS=ON
    -DCMAKE_DISABLE_FIND_PACKAGE_KF6DocTools=ON -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_QDOC=OFF
    ${privacy_options}
    RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Patched KIO configure failed.")
endif()
file(READ "${binary}/compile_commands.json" commands)
if(commands MATCHES "faviconscache")
    message(FATAL_ERROR "Favicon cache still appears in compile commands; refusing to build.")
endif()
smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${binary}" --parallel 4)
smoothmail_dependency_command("${CMAKE_COMMAND}" --install "${binary}")
set(notices "${prefix}/share/licenses/kio")
file(MAKE_DIRECTORY "${notices}")
file(COPY "${pristine}/LICENSES" "${patch}" "${CMAKE_CURRENT_LIST_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION "${notices}")
smoothmail_write_modified_receipt("${notices}" "${prefix}/bin/KF6KIOCore.dll" "${commit}" "${patch}")
message(STATUS "Patched KIO installed. The development prefix is NOT a product distribution.")
