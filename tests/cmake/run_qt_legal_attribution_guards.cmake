cmake_minimum_required(VERSION 3.30)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef nonce)
foreach(case IN ITEMS valid escape missing type entry-type)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}"
        "-DROOT=${repository}/build/qt-legal-attribution-guards/${nonce}/${case}"
        -P "${CMAKE_CURRENT_LIST_DIR}/qt_legal_attribution_fixture.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(case STREQUAL valid AND NOT result STREQUAL "0")
        message(FATAL_ERROR "Qt attribution original collection failed: ${out}\n${err}")
    elseif(NOT case STREQUAL valid AND result STREQUAL "0")
        message(FATAL_ERROR "Unsafe Qt attribution accepted: ${case}")
    endif()
endforeach()
message(STATUS "5 Qt singular/plural license/copyright attribution guards passed.")
