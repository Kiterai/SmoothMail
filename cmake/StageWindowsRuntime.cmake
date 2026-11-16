cmake_minimum_required(VERSION 3.25)

# Developer-only runtime staging. Never copy the whole KDE/native prefix or
# treat Debug CRT staging as a redistributable product.
foreach(required IN ITEMS APP QT_BIN DEPENDENCY_BIN NATIVE_BIN SASL_DIR)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Missing ${required}: ${${required}}")
    endif()
endforeach()
if(NOT CONFIGURATION STREQUAL Debug)
    message(FATAL_ERROR "This developer staging script currently supports Debug only; Release packaging requires a separate license/runtime audit.")
endif()
if(NOT DEFINED DESTINATION OR DESTINATION STREQUAL "")
    message(FATAL_ERROR "Choose a new staging directory.")
endif()
file(MAKE_DIRECTORY "${DESTINATION}")
file(GLOB existing "${DESTINATION}/*")
if(existing)
    message(FATAL_ERROR "Staging directory must be empty; existing files are not removed: ${DESTINATION}")
endif()
find_program(DUMPBIN dumpbin REQUIRED)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL dumpbin)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DUMPBIN}")

file(COPY "${APP}" DESTINATION "${DESTINATION}")
get_filename_component(app_name "${APP}" NAME)
get_filename_component(qt_root "${QT_BIN}" DIRECTORY)
# Plugins loaded at runtime do not necessarily appear in the EXE imports.
set(plugins platforms/qwindowsd.dll sqldrivers/qsqlited.dll tls/qschannelbackendd.dll)
set(plugin_files)
foreach(plugin IN LISTS plugins)
    get_filename_component(kind "${plugin}" DIRECTORY)
    file(MAKE_DIRECTORY "${DESTINATION}/${kind}")
    file(COPY "${qt_root}/plugins/${plugin}" DESTINATION "${DESTINATION}/${kind}")
    list(APPEND plugin_files "${qt_root}/plugins/${plugin}")
endforeach()
set(sasl_plugins)
foreach(mechanism IN ITEMS PLAIN LOGIN XOAuth2)
    set(plugin "${SASL_DIR}/sasl${mechanism}.dll")
    if(NOT EXISTS "${plugin}")
        message(FATAL_ERROR "Required SASL mechanism is missing: ${plugin}")
    endif()
    list(APPEND sasl_plugins "${plugin}")
endforeach()
file(MAKE_DIRECTORY "${DESTINATION}/sasl2")
file(COPY ${sasl_plugins} DESTINATION "${DESTINATION}/sasl2")

# Resolve imports recursively, including third-party libraries that
# windeployqt does not deploy. The current MSVC SDK supplies Debug CRT only
# for this developer machine / disposable testing environment.
file(GLOB crt_directories "$ENV{VCToolsRedistDir}/debug_nonredist/x64/Microsoft.VC*.DebugCRT")
set(ucrt_directory "$ENV{UniversalCRTSdkDir}/bin/$ENV{UCRTVersion}/x64/ucrt")
if(NOT crt_directories OR NOT EXISTS "${ucrt_directory}/ucrtbased.dll")
    message(FATAL_ERROR "Run from an x64 MSVC developer environment with Debug CRT/UCRT installed.")
endif()
# Stop at OS imports rather than recursively walking Windows' optional/delay
# imports (e.g. Application Guard's HvsiFileTrust.dll). OS DLLs are not staged.
file(GLOB system_libraries "$ENV{SystemRoot}/System32/*.dll")
set(system_patterns)
foreach(system_library IN LISTS system_libraries)
    get_filename_component(system_name "${system_library}" NAME)
    string(TOLOWER "${system_name}" system_name)
    if(NOT system_name STREQUAL "ucrtbased.dll")
        string(REPLACE "." "\\." system_name "${system_name}")
        list(APPEND system_patterns "^${system_name}$")
    endif()
endforeach()
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${APP}"
    LIBRARIES ${plugin_files} ${sasl_plugins}
    DIRECTORIES "${DEPENDENCY_BIN}" "${NATIVE_BIN}" "${QT_BIN}" ${crt_directories} "${ucrt_directory}"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" ${system_patterns}
    POST_EXCLUDE_REGEXES "[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*"
    RESOLVED_DEPENDENCIES_VAR resolved UNRESOLVED_DEPENDENCIES_VAR unresolved)
if(unresolved)
    message(FATAL_ERROR "Unresolved runtime dependencies: ${unresolved}")
endif()
set(qt_libraries)
foreach(library IN LISTS resolved)
    get_filename_component(library_directory "${library}" DIRECTORY)
    if(library_directory STREQUAL QT_BIN)
        list(APPEND qt_libraries "${library}")
    endif()
    file(COPY "${library}" DESTINATION "${DESTINATION}")
endforeach()
# A developer host may already have Debug CRT in System32. Always stage the
# selected toolchain's copy explicitly, since a clean guest will not have it.
foreach(crt_directory IN LISTS crt_directories)
    file(GLOB crt_files "${crt_directory}/*.dll")
    file(COPY ${crt_files} DESTINATION "${DESTINATION}")
endforeach()
file(COPY "${ucrt_directory}/ucrtbased.dll" DESTINATION "${DESTINATION}")
# Qt 6.12 windeployqt mistakes qt6keychaind.dll for an SDK module and tries
# to read it from the SDK's bin directory. Feed only actual SDK DLLs discovered
# above, rather than changing the SDK or ignoring missing-library failures.
if(NOT qt_libraries)
    message(FATAL_ERROR "No Qt SDK DLLs resolved.")
endif()
execute_process(COMMAND "${QT_BIN}/windeployqt.exe" --debug --no-compiler-runtime
    --no-translations --no-plugins --no-opengl-sw --no-system-d3d-compiler
    --no-system-dxc-compiler --no-ffmpeg --no-patchqt
    --qtpaths "${QT_BIN}/qtpaths.exe" --dir "${DESTINATION}" ${qt_libraries}
    RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed: ${deploy_result}")
endif()
file(WRITE "${DESTINATION}/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\n")
file(WRITE "${DESTINATION}/start-smoothmail.cmd"
    "@echo off\r\nsetlocal\r\ncd /d \"%~dp0\"\r\nset \"QT_PLUGIN_PATH=%~dp0\"\r\nset \"QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0platforms\"\r\nset \"QT_QPA_PLATFORM=windows\"\r\nset \"SMOOTHMAIL_SASL_PATH=%~dp0sasl2\"\r\nstart \"\" \"%~dp0${app_name}\"\r\n")
file(WRITE "${DESTINATION}/DEVELOPMENT-ONLY.txt"
    "SmoothMail Debug developer runtime. NOT a release/distribution package.\nContains non-redistributable Debug CRT for local developer/disposable tests only.\nOAuth defaults are embedded at build time. Never put passwords or refresh tokens here.\n")
file(GLOB_RECURSE staged_files LIST_DIRECTORIES false "${DESTINATION}/*")
set(inventory "SHA256  relative path\n")
foreach(file IN LISTS staged_files)
    file(SHA256 "${file}" hash)
    file(RELATIVE_PATH relative "${DESTINATION}" "${file}")
    string(APPEND inventory "${hash}  ${relative}\n")
endforeach()
file(WRITE "${DESTINATION}/runtime-sha256.txt" "${inventory}")
message(STATUS "Developer runtime staged: ${DESTINATION}")
