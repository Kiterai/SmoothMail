cmake_minimum_required(VERSION 3.30)
file(DOWNLOAD "https://pypi.org/" TLS_VERIFY ON TLS_VERSION 1.2
    STATUS status LOG log TIMEOUT 30)
list(GET status 0 code)
message(STATUS "CMake=${CMAKE_VERSION}; status=${code}")
foreach(marker IN ITEMS "schannel" "SEC_E_NO_CREDENTIALS" "SSL connection using" "OpenSSL")
    string(FIND "${log}" "${marker}" position)
    if(NOT position EQUAL -1)
        message(STATUS "TLS diagnostic marker: ${marker}")
    endif()
endforeach()
# Never print raw libcurl logs: they may contain proxy credentials.
