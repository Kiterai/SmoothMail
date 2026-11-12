include_guard(GLOBAL)

if(NOT DEFINED Qt6_ROOT OR NOT EXISTS "${Qt6_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR "Specify an installed Qt SDK using -DQt6_ROOT=/path/to/Qt/sdk")
endif()
if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release|RelWithDebInfo|MinSizeRel)$")
    message(FATAL_ERROR "Unsupported CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()
if(NOT DEFINED DEPENDENCY_ROOT)
    set(DEPENDENCY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../build/deps")
endif()
get_filename_component(DEPENDENCY_ROOT "${DEPENDENCY_ROOT}" ABSOLUTE)
get_filename_component(Qt6_ROOT "${Qt6_ROOT}" ABSOLUTE)
set(prefix "${DEPENDENCY_ROOT}/install/${CMAKE_BUILD_TYPE}")
find_package(Git REQUIRED)
if(NOT DEFINED CMAKE_MAKE_PROGRAM)
    find_program(CMAKE_MAKE_PROGRAM ninja REQUIRED)
endif()
file(READ "${CMAKE_CURRENT_LIST_DIR}/dependencies.json" manifest)

function(smoothmail_dependency_command)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Dependency command failed (${result}): ${ARGV}")
    endif()
endfunction()

function(smoothmail_build_dependency dependency)
    string(JSON repository GET "${manifest}" "${dependency}" repository)
    string(JSON tag GET "${manifest}" "${dependency}" tag)
    string(JSON commit GET "${manifest}" "${dependency}" commit)
    set(source "${DEPENDENCY_ROOT}/src/${dependency}")
    set(binary "${DEPENDENCY_ROOT}/build/${CMAKE_BUILD_TYPE}/${dependency}")
    if(NOT EXISTS "${source}")
        smoothmail_dependency_command("${GIT_EXECUTABLE}" clone --depth 1 --branch "${tag}" "${repository}" "${source}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" rev-parse HEAD
        OUTPUT_VARIABLE actual OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result STREQUAL "0" OR NOT actual STREQUAL commit)
        message(FATAL_ERROR "${source} is not pinned commit ${commit}; use a new dependency directory.")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" status --porcelain --untracked-files=no
        OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result STREQUAL "0" OR NOT dirty STREQUAL "")
        message(FATAL_ERROR "${source} has modified tracked files; refusing to build.")
    endif()

    # Invoke directly so the prefix search list remains a single argument.
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
        "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "-DCMAKE_INSTALL_PREFIX=${prefix}"
        "-DCMAKE_PREFIX_PATH=${prefix};${Qt6_ROOT}"
        "-DQt6_ROOT=${Qt6_ROOT}"
        -DBUILD_SHARED_LIBS=ON -DBUILD_TESTING=OFF -DBUILD_DOC=OFF -DBUILD_QCH=OFF
        -DBUILD_QDOC=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF
        ${ARGN}
        RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Configure failed for ${dependency}")
    endif()
    smoothmail_dependency_command("${CMAKE_COMMAND}" --build "${binary}" --parallel 4)
    smoothmail_dependency_command("${CMAKE_COMMAND}" --install "${binary}")
endfunction()
