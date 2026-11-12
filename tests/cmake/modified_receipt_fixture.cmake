cmake_minimum_required(VERSION 3.25)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ModifiedDependencyReceipt.cmake")
smoothmail_validate_modified_receipt("${RECEIPT}" "${ARTIFACT}" "${QTCORE}" "fixture-commit" "${PATCH}" "Debug")
