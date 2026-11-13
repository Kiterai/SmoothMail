cmake_minimum_required(VERSION 3.30)
foreach(case IN ITEMS cache hash scheme mismatch)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DCASE=${case}"
        -P "${CMAKE_CURRENT_LIST_DIR}/download_guards.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(case STREQUAL "cache")
        if(NOT result STREQUAL "0")
            message(FATAL_ERROR "Verified cache reuse failed")
        endif()
    elseif(result STREQUAL "0")
        message(FATAL_ERROR "Download guard accepted ${case}")
    endif()
endforeach()
message(STATUS "4 download guards passed (offline).")
