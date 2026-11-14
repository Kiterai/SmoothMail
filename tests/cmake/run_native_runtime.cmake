cmake_minimum_required(VERSION 3.30)
foreach(required IN ITEMS PROBE NATIVE_BIN)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Missing native runtime test input: ${required}")
    endif()
endforeach()
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef nonce)
set(root "${repository}/build/native-runtime-tests/${nonce}")
if(EXISTS "${root}")
    message(FATAL_ERROR "Native test fixture already exists.")
endif()
set(first "${root}/initial")
set(second "${root}/moved path-再配置")
file(MAKE_DIRECTORY "${first}" "${second}")
foreach(dll IN ITEMS intl-8.dll iconv-2.dll)
    file(COPY_FILE "${NATIVE_BIN}/${dll}" "${first}/${dll}")
endforeach()
# Seed before process creation: Qt Debug and GNU Release use different CRT
# environment snapshots, so qputenv alone is not sufficient for this mixed test.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env LANGUAGE=zz "${PROBE}" "${first}"
    WORKING_DIRECTORY "${root}" RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Native runtime test failed at initial location.")
endif()
# Includes the synthetic catalog written by the first probe. Never run from the
# DLL directory, and do not depend on the builder's original install prefix.
file(COPY "${first}/" DESTINATION "${second}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env LANGUAGE=zz "${PROBE}" "${second}"
    WORKING_DIRECTORY "${root}" RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Native runtime test failed after relocation.")
endif()
message(STATUS "Native runtime works at both isolated locations.")
# This exact random directory was created above and contains only owned fixtures.
file(REMOVE_RECURSE "${root}")
