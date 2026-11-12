cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PrepareKioSource.cmake")
smoothmail_prepare_kio_source("${PRISTINE}" "${COMMIT}" "${PATCH}" "${WORKSPACE}" prepared)
message(STATUS "Prepared source: ${prepared}")
