cmake_minimum_required(VERSION 3.30)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PrepareSaslSource.cmake")
smoothmail_prepare_sasl_source("${ARCHIVE}" "${WORKSPACE}" source)
