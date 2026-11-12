cmake_minimum_required(VERSION 3.30)

# Explicit opt-in bootstrap, never run automatically by application configure.
# Run from the same compiler environment used to build SmoothMail.
include("${CMAKE_CURRENT_LIST_DIR}/DependencyBuild.cmake")

foreach(dependency IN ITEMS extra-cmake-modules kcodecs kmime)
    smoothmail_build_dependency("${dependency}")
endforeach()

message(STATUS "MIME dependency prefix: ${prefix}")
message(STATUS "KIMAP/KSMTP/SASL are NOT built by this MIME-only bootstrap.")
