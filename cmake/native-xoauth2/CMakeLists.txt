cmake_minimum_required(VERSION 3.30)
project(SmoothMailNativeXOAuth2 LANGUAGES C)
if(NOT MSVC OR NOT WIN32)
    message(FATAL_ERROR "This compatibility wrapper is for native MSVC Windows builds.")
endif()
find_path(SASL_INCLUDE_DIR sasl/sasl.h PATHS "${SASL_PREFIX}/include" NO_DEFAULT_PATH REQUIRED)
configure_file(config.h.in config.h COPYONLY)
add_library(saslXOAuth2 MODULE
    "${XOAUTH2_SOURCE}/xoauth2_client.c"
    "${XOAUTH2_SOURCE}/xoauth2_str.c"
    client-entry.c)
target_compile_definitions(saslXOAuth2 PRIVATE HAVE_CONFIG_H WIN32)
target_include_directories(saslXOAuth2 PRIVATE "${SASL_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(saslXOAuth2 PROPERTIES PREFIX "")
install(TARGETS saslXOAuth2 LIBRARY DESTINATION bin/sasl2 RUNTIME DESTINATION bin/sasl2)
install(FILES "${XOAUTH2_SOURCE}/COPYING" DESTINATION share/licenses/cyrus-sasl-xoauth2)
