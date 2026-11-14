cmake_minimum_required(VERSION 3.30)

# Separate from developer staging: Debug CRT and SDK mutation are never allowed.
foreach(required IN ITEMS APP QT_BIN DEPENDENCY_BIN NATIVE_BIN SASL_DIR CRT_BIN)
    if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Missing ${required}")
    endif()
    file(TO_CMAKE_PATH "${${required}}" ${required})
endforeach()
if(NOT CONFIGURATION STREQUAL "Release")
    message(FATAL_ERROR "Distribution requires Release")
endif()
if(NOT DEFINED DESTINATION OR DESTINATION STREQUAL "")
    message(FATAL_ERROR "Choose a new staging directory")
endif()
file(TO_CMAKE_PATH "${DESTINATION}" DESTINATION)
file(GLOB existing "${DESTINATION}/*" "${DESTINATION}/.*")
if(existing)
    message(FATAL_ERROR "Staging directory must be empty")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/WindowsCrt.cmake")
smoothmail_validate_crt_directory("${CRT_BIN}")
find_program(DUMPBIN dumpbin REQUIRED)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL dumpbin)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${DUMPBIN}")
get_filename_component(qt_root "${QT_BIN}" DIRECTORY)
set(plugins platforms/qwindows.dll sqldrivers/qsqlite.dll tls/qschannelbackend.dll)
set(plugin_files)
foreach(plugin IN LISTS plugins)
    if(NOT EXISTS "${qt_root}/plugins/${plugin}")
        message(FATAL_ERROR "Missing Release Qt plugin: ${plugin}")
    endif()
    list(APPEND plugin_files "${qt_root}/plugins/${plugin}")
endforeach()
set(sasl_plugins)
foreach(mechanism IN ITEMS PLAIN LOGIN XOAuth2)
    if(NOT EXISTS "${SASL_DIR}/sasl${mechanism}.dll")
        message(FATAL_ERROR "Missing Release SASL mechanism: ${mechanism}")
    endif()
    list(APPEND sasl_plugins "${SASL_DIR}/sasl${mechanism}.dll")
endforeach()
# Never consider every DLL installed in the builder's System32 an OS dependency:
# it may contain third-party runtimes absent from a clean Windows installation.
set(system_libraries advapi32.dll authz.dll bcrypt.dll cfgmgr32.dll comctl32.dll
    comdlg32.dll crypt32.dll d2d1.dll d3d9.dll d3d11.dll d3d12.dll dbghelp.dll
    dnsapi.dll dwmapi.dll dwrite.dll dxgi.dll gdi32.dll icuuc.dll imm32.dll
    iphlpapi.dll kernel32.dll ktmw32.dll mpr.dll mscms.dll msimg32.dll mswsock.dll msvcrt.dll msvcp_win.dll
    ncrypt.dll netapi32.dll normaliz.dll ntdll.dll ole32.dll oleaut32.dll opengl32.dll
    powrprof.dll propsys.dll psapi.dll rpcrt4.dll runtimeobject.dll secur32.dll
    setupapi.dll shcore.dll shell32.dll shlwapi.dll ucrtbase.dll uiautomationcore.dll
    user32.dll userenv.dll uxtheme.dll version.dll windowscodecs.dll winhttp.dll
    wininet.dll winmm.dll winspool.drv ws2_32.dll wsock32.dll wtsapi32.dll wldap32.dll
    # Inspect these imports separately so the host System32/SDK copy never
    # substitutes for the explicitly supplied redistributable CRT.
    msvcp140.dll msvcp140_1.dll msvcp140_2.dll msvcp140_atomic_wait.dll
    msvcp140_codecvt_ids.dll vcruntime140.dll vcruntime140_1.dll
    vcruntime140_threads.dll concrt140.dll vccorlib140.dll)
set(system_patterns)
foreach(library IN LISTS system_libraries)
    get_filename_component(name "${library}" NAME)
    string(TOLOWER "${name}" name)
    # Debug CRT must not be treated as a redistributable system import.
    if(NOT name MATCHES "^(ucrtbased|msvcp.*d(_.*)?|vcruntime.*d|concrt.*d)\\.dll$")
        string(REPLACE "." "\\." name "${name}")
        list(APPEND system_patterns "^${name}$")
    endif()
endforeach()
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${APP}" LIBRARIES ${plugin_files} ${sasl_plugins}
    DIRECTORIES "${DEPENDENCY_BIN}" "${NATIVE_BIN}" "${QT_BIN}" "${CRT_BIN}"
    PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" ${system_patterns}
    RESOLVED_DEPENDENCIES_VAR resolved UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts)
if(unresolved OR conflicts_FILENAMES)
    message(FATAL_ERROR "Unresolved or conflicting Release runtime imports")
endif()
set(allowed "${DEPENDENCY_BIN}" "${NATIVE_BIN}" "${QT_BIN}" "${CRT_BIN}")
set(qt_libraries)
foreach(library IN LISTS resolved)
    get_filename_component(name "${library}" NAME)
    string(TOLOWER "${name}" lower)
    if(lower MATCHES "^(qt6.*d|ucrtbased|msvcp.*d(_.*)?|vcruntime.*d|concrt.*d)\\.dll$")
        message(FATAL_ERROR "Debug dependency is not distributable: ${name}")
    endif()
    get_filename_component(directory "${library}" DIRECTORY)
    file(REAL_PATH "${directory}" directory)
    set(found FALSE)
    foreach(prefix IN LISTS allowed)
        file(REAL_PATH "${prefix}" prefix)
        if(directory STREQUAL prefix)
            set(found TRUE)
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "Runtime resolved outside the explicit input directories: ${name}")
    endif()
    if(directory STREQUAL QT_BIN)
        list(APPEND qt_libraries "${library}")
    endif()
endforeach()
smoothmail_collect_crt("${CRT_BIN}" "${APP};${resolved};${plugin_files};${sasl_plugins}" crt_files)
smoothmail_prepare_crt_verifier(crt_verifier)
# Fail before staging if a required CRT is unsigned, modified, wrong-architecture,
# or from a different version set. The verifier never prompts or uses the network.
foreach(library IN LISTS crt_files)
    execute_process(COMMAND "${crt_verifier}" "${library}" "${library}" "${CRT_BIN}/vcruntime140.dll"
        RESULT_VARIABLE result OUTPUT_QUIET)
    if(NOT result STREQUAL "0")
        get_filename_component(name "${library}" NAME)
        message(FATAL_ERROR "Release redistributable CRT verification failed: ${name}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${DESTINATION}" "${DESTINATION}/sasl2")
file(COPY "${APP}" ${resolved} ${crt_files} DESTINATION "${DESTINATION}")
foreach(plugin IN LISTS plugins)
    get_filename_component(kind "${plugin}" DIRECTORY)
    file(MAKE_DIRECTORY "${DESTINATION}/${kind}")
    file(COPY "${qt_root}/plugins/${plugin}" DESTINATION "${DESTINATION}/${kind}")
endforeach()
file(COPY ${sasl_plugins} DESTINATION "${DESTINATION}/sasl2")
if(NOT qt_libraries)
    message(FATAL_ERROR "No Release Qt runtime libraries resolved")
endif()
# Feed SDK libraries only (QtKeychain is not a Qt SDK module). Patch the staged
# Qt prefix for relocation; never modify the developer's installed SDK.
execute_process(COMMAND "${QT_BIN}/windeployqt.exe" --release --no-compiler-runtime
    --no-translations --no-plugins --no-opengl-sw --no-system-d3d-compiler
    --no-system-dxc-compiler --no-ffmpeg --qtpaths "${QT_BIN}/qtpaths.exe"
    --dir "${DESTINATION}" ${qt_libraries} RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
    message(FATAL_ERROR "Release Qt deployment failed")
endif()
foreach(library IN LISTS crt_files)
    get_filename_component(name "${library}" NAME)
    execute_process(COMMAND "${crt_verifier}" "${library}" "${DESTINATION}/${name}" "${CRT_BIN}/vcruntime140.dll"
        RESULT_VARIABLE result OUTPUT_VARIABLE evidence)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Staged CRT verification failed: ${name}")
    endif()
    string(STRIP "${evidence}" evidence)
    message(STATUS "CRT verified: ${name} ${evidence}")
endforeach()
file(WRITE "${DESTINATION}/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\n")
# No launcher, developer cache, PDB, environment dump or build receipts in the ZIP.
