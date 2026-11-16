include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/DownloadAsset.cmake")

function(smoothmail_configure_vcpkg_asset_transport)
    if(SMOOTHMAIL_DOWNLOAD_CURL)
        smoothmail_validate_download_curl()
    endif()
    # x-block-origin prevents an implicit fallback to WinHTTP/Schannel.
    set(ENV{X_VCPKG_ASSET_SOURCES}
        "clear;x-script,\"${CMAKE_COMMAND}\" \"-DSMOOTHMAIL_DOWNLOAD_CURL=${SMOOTHMAIL_DOWNLOAD_CURL}\" \"-DURL={url}\" \"-DSHA512={sha512}\" \"-DDESTINATION={dst}\" -P \"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FetchVcpkgAsset.cmake\";x-block-origin")
endfunction()
