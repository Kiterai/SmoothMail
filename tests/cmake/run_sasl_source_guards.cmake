cmake_minimum_required(VERSION 3.30)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef nonce)
set(root "${repository}/build/sasl-source-tests/${nonce}")
set(pristine "${root}/input/cyrus-sasl-2.1.28")
file(MAKE_DIRECTORY "${pristine}/win32/include" "${pristine}/include")
set(config [=[#define HAVE_MEMCPY 1
#define HAVE_OPENSSL 1

#define PLUGINDIR "C:\\CMU\\bin\\sasl2"
#define CONFIGDIR "C:\\CMU\\bin\\sasl2"

/* Windows calls these functions something else
 */
]=])
file(WRITE "${pristine}/win32/include/config.h" "${config}")
file(WRITE "${pristine}/win32/include/md5global.h" "/* fixture */\n")
set(archive "${root}/original.tar")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=gnutar cyrus-sasl-2.1.28
    WORKING_DIRECTORY "${root}/input" COMMAND_ERROR_IS_FATAL ANY)
file(SHA256 "${archive}" before)
include("${repository}/cmake/PrepareSaslSource.cmake")
smoothmail_prepare_sasl_source("${archive}" "${root}/work" source)
smoothmail_prepare_sasl_source("${archive}" "${root}/work" repeated)
file(READ "${pristine}/win32/include/config.h" original)
file(READ "${source}/win32/include/config.h" patched)
file(SHA256 "${archive}" after)
if(NOT before STREQUAL after OR NOT original STREQUAL config OR NOT source STREQUAL repeated
        OR NOT patched MATCHES "#define PLUGINDIR \"\"" OR NOT patched MATCHES "#define CONFIGDIR \"\"")
    message(FATAL_ERROR "SASL patch isolation/reuse failed.")
endif()
file(APPEND "${source}/win32/include/config.h" "/* user edit */\n")
execute_process(COMMAND "${CMAKE_COMMAND}" "-DARCHIVE=${archive}" "-DWORKSPACE=${root}/work"
    -P "${CMAKE_CURRENT_LIST_DIR}/prepare_sasl_fixture.cmake" RESULT_VARIABLE result
    OUTPUT_QUIET ERROR_QUIET)
file(READ "${source}/win32/include/config.h" preserved)
if(result STREQUAL "0" OR NOT preserved MATCHES "user edit")
    message(FATAL_ERROR "Modified SASL configuration was not rejected/preserved.")
endif()
message(STATUS "SASL archive preservation, isolated path patch, reuse and tamper rejection passed.")
