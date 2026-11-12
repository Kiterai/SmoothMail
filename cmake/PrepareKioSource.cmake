include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/PreparePatchedSource.cmake")

# Keep existing KIO copy paths and receipts valid.
function(smoothmail_prepare_kio_source pristine commit patch workspace output_variable)
    smoothmail_prepare_patched_source(kio "${pristine}" "${commit}" "${patch}" "${workspace}" source)
    set("${output_variable}" "${source}" PARENT_SCOPE)
endfunction()
