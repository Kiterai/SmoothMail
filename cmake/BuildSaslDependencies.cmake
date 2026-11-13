cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")
if(NOT WIN32)
    message(FATAL_ERROR "This bootstrap currently supports native Windows builds only.")
endif()
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
    message(FATAL_ERROR "The native SASL bootstrap supports Debug or Release only.")
endif()
find_program(NMAKE_EXECUTABLE nmake REQUIRED)

set(sha256 "7ccfc6abd01ed67c1a0924b353e526f1b766b21f42d4562ee635a8ebfc5bb38c")
set(archive "${DEPENDENCY_ROOT}/downloads/cyrus-sasl-2.1.28.tar.gz")
smoothmail_download_asset(
    "https://github.com/cyrusimap/cyrus-sasl/releases/download/cyrus-sasl-2.1.28/cyrus-sasl-2.1.28.tar.gz"
    SHA256 "${sha256}" "${archive}")
set(work "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/cyrus-sasl-${sha256}")
set(source "${work}/cyrus-sasl-2.1.28")
if(NOT EXISTS "${source}")
    file(MAKE_DIRECTORY "${work}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}")
    file(COPY_FILE "${source}/win32/include/md5global.h" "${source}/include/md5global.h")
endif()
if(CMAKE_BUILD_TYPE STREQUAL Debug)
    set(compiler_flags "/MDd /W3 /EHsc /Zi /Od /D_DEBUG")
else()
    set(compiler_flags "/MD /W3 /EHsc /Zi /O2 /DNDEBUG")
endif()
# Build only the required client mechanisms; no OpenSSL, SASL database,
# server utilities or obsolete authentication mechanisms are pulled in.
foreach(component IN ITEMS lib plugins)
    if(component STREQUAL lib)
        set(targets all)
    else()
        set(targets saslPLAIN.dll saslLOGIN.dll)
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "CL=/DUNICODE /D_UNICODE /D_WINSOCK_DEPRECATED_NO_WARNINGS"
        "${NMAKE_EXECUTABLE}" /NOLOGO /f NTMakefile "CFG=${CMAKE_BUILD_TYPE}" STATIC=no
        "CPP_PROJ=${compiler_flags} /D_CRT_SECURE_NO_DEPRECATE=1 $(CPPFLAGS) /c"
        "LINK32_FLAGS=/debug" ${targets}
        WORKING_DIRECTORY "${source}/${component}" RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Cyrus SASL ${component} build failed: ${result}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${prefix}/bin/sasl2" "${prefix}/lib" "${prefix}/include/sasl"
    "${prefix}/share/licenses/cyrus-sasl")
file(COPY_FILE "${source}/lib/libsasl.dll" "${prefix}/bin/libsasl.dll")
file(COPY_FILE "${source}/lib/libsasl.lib" "${prefix}/lib/libsasl2.lib")
file(COPY "${source}/plugins/saslPLAIN.dll" "${source}/plugins/saslLOGIN.dll" DESTINATION "${prefix}/bin/sasl2")
foreach(header IN ITEMS sasl.h saslplug.h saslutil.h prop.h hmac-md5.h md5.h md5global.h)
    file(COPY "${source}/include/${header}" DESTINATION "${prefix}/include/sasl")
endforeach()
file(COPY "${source}/COPYING" DESTINATION "${prefix}/share/licenses/cyrus-sasl")
message(STATUS "SASL PLAIN/LOGIN built in ${prefix}; XOAUTH2 is a separate plugin.")
