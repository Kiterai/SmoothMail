cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/PreparePatchedSource.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ModifiedDependencyReceipt.cmake")
if(NOT WIN32)
    message(FATAL_ERROR "This mail dependency bootstrap is currently validated for Windows only.")
endif()
get_filename_component(patch "${CMAKE_CURRENT_LIST_DIR}/../patches/kio/6.31.0-disable-favicon-cache.patch" ABSOLUTE)
set(installed_patch "${prefix}/share/licenses/kio/6.31.0-disable-favicon-cache.patch")
if(NOT EXISTS "${installed_patch}")
    message(FATAL_ERROR "Run BuildKioDependencies.cmake to build the approved patched KIO first.")
endif()
file(SHA256 "${patch}" expected_hash)
file(SHA256 "${installed_patch}" installed_hash)
if(NOT expected_hash STREQUAL installed_hash)
    message(FATAL_ERROR "Installed KIO modification notice does not match the current patch.")
endif()
set(native "${SMOOTHMAIL_NATIVE_PREFIX}")
set(gettext_tools "${SMOOTHMAIL_GETTEXT_TOOLS_BIN}")
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(native_bin "${native}/debug/bin")
else()
    set(native_bin "${native}/bin")
endif()
set(ENV{PATH} "${native_bin};${gettext_tools};${prefix}/bin;${Qt6_ROOT}/bin;$ENV{PATH}")
smoothmail_fetch_dependency(kimap pristine)
string(JSON commit GET "${manifest}" kimap commit)
get_filename_component(kimap_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/kimap/26.08.3-fix-sasl-continuation.patch" ABSOLUTE)
smoothmail_prepare_patched_source(kimap "${pristine}" "${commit}" "${kimap_patch}"
    "${DEPENDENCY_ROOT}/patched-src" source)
smoothmail_build_dependency_source(kimap "${source}"
    "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/kimap-sasl-continuation"
    "-DGETTEXT_MSGFMT_EXECUTABLE=${gettext_tools}/msgfmt.exe"
    "-DGETTEXT_MSGMERGE_EXECUTABLE=${gettext_tools}/msgmerge.exe")
set(notices "${prefix}/share/licenses/kimap")
file(MAKE_DIRECTORY "${notices}")
file(COPY "${pristine}/LICENSES" "${kimap_patch}" "${CMAKE_CURRENT_LIST_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION "${notices}")
smoothmail_write_modified_receipt("${notices}" "${prefix}/bin/KPim6IMAP.dll" "${commit}" "${kimap_patch}")
smoothmail_fetch_dependency(ksmtp smtp_pristine)
string(JSON smtp_commit GET "${manifest}" ksmtp commit)
get_filename_component(smtp_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/ksmtp/26.08.3-reset-ehlo-capabilities.patch" ABSOLUTE)
smoothmail_prepare_patched_source(ksmtp "${smtp_pristine}" "${smtp_commit}" "${smtp_patch}"
    "${DEPENDENCY_ROOT}/patched-src" smtp_source)
smoothmail_build_dependency_source(ksmtp "${smtp_source}"
    "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/ksmtp-fresh-ehlo"
    "-DGETTEXT_MSGFMT_EXECUTABLE=${gettext_tools}/msgfmt.exe"
    "-DGETTEXT_MSGMERGE_EXECUTABLE=${gettext_tools}/msgmerge.exe")
set(smtp_notices "${prefix}/share/licenses/ksmtp")
file(MAKE_DIRECTORY "${smtp_notices}")
file(COPY "${smtp_pristine}/LICENSES" "${smtp_patch}" "${CMAKE_CURRENT_LIST_DIR}/../THIRD_PARTY_NOTICES.md"
    DESTINATION "${smtp_notices}")
smoothmail_write_modified_receipt("${smtp_notices}" "${prefix}/bin/KPim6SMTP.dll" "${smtp_commit}" "${smtp_patch}")
message(STATUS "KIMAP/KSMTP built; server authentication and distribution remain separate tests.")
