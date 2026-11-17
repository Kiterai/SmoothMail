# Application declarations shared by real builds and the SDK-free documentation model.
function(smoothmail_add_library target kind)
    if(SMOOTHMAIL_DOCUMENTATION_ONLY)
        add_library(${target} INTERFACE)
    else()
        add_library(${target} ${kind} ${ARGN})
    endif()
    set_property(TARGET ${target} PROPERTY SMOOTHMAIL_DOCUMENTED_KIND "${kind}")
    set_property(TARGET ${target} PROPERTY SMOOTHMAIL_DOCUMENTED_SOURCES "${ARGN}")
endfunction()

function(smoothmail_add_sources target visibility)
    if(NOT SMOOTHMAIL_DOCUMENTATION_ONLY)
        target_sources(${target} ${visibility} ${ARGN})
    endif()
    set_property(TARGET ${target} APPEND PROPERTY SMOOTHMAIL_DOCUMENTED_SOURCES "${ARGN}")
endfunction()

function(smoothmail_include_directories target)
    if(NOT SMOOTHMAIL_DOCUMENTATION_ONLY)
        target_include_directories(${target} ${ARGN})
    endif()
endfunction()

function(smoothmail_link_libraries target)
    if(SMOOTHMAIL_DOCUMENTATION_ONLY)
        set(libraries ${ARGN})
        list(REMOVE_ITEM libraries PUBLIC PRIVATE INTERFACE)
        foreach(library IN LISTS libraries)
            if(library MATCHES "::" AND NOT TARGET "${library}")
                add_library("${library}" INTERFACE IMPORTED GLOBAL)
            endif()
        endforeach()
        target_link_libraries(${target} INTERFACE ${libraries})
    else()
        target_link_libraries(${target} ${ARGN})
    endif()
endfunction()

function(smoothmail_add_application target)
    if(SMOOTHMAIL_DOCUMENTATION_ONLY)
        add_library(${target} INTERFACE)
        set(sources ${ARGN})
        list(REMOVE_ITEM sources WIN32)
    else()
        qt_add_executable(${target} ${ARGN})
        set(sources ${ARGN})
        list(REMOVE_ITEM sources WIN32)
    endif()
    set_property(TARGET ${target} PROPERTY SMOOTHMAIL_DOCUMENTED_KIND EXECUTABLE)
    set_property(TARGET ${target} PROPERTY SMOOTHMAIL_DOCUMENTED_SOURCES "${sources}")
endfunction()

smoothmail_add_library(smoothmail_diagnostics STATIC src/core/diagnostics.cpp src/core/diagnostics.h)
smoothmail_include_directories(smoothmail_diagnostics PUBLIC src)
smoothmail_link_libraries(smoothmail_diagnostics PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_diagnostics)
smoothmail_add_library(smoothmail_connection_policy STATIC src/mail/connectionpolicy.cpp src/mail/connectionpolicy.h)
smoothmail_include_directories(smoothmail_connection_policy PUBLIC src)
smoothmail_link_libraries(smoothmail_connection_policy PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_connection_policy)
smoothmail_add_library(smoothmail_oauth STATIC src/oauth/oauthclient.cpp src/oauth/oauthclient.h)
smoothmail_include_directories(smoothmail_oauth PUBLIC src)
smoothmail_link_libraries(smoothmail_oauth PUBLIC Qt6::Core Qt6::Network)
smoothmail_target_defaults(smoothmail_oauth)
smoothmail_add_library(smoothmail_storage STATIC src/core/account.cpp src/core/account.h src/core/connectionsettings.h
    src/storage/accountrepository.cpp src/storage/accountrepository.h
    src/storage/accountregistrar.cpp src/storage/accountregistrar.h
    src/storage/credentialstore.cpp src/storage/credentialstore.h)
smoothmail_add_sources(smoothmail_storage PRIVATE src/storage/downloadfile.cpp src/storage/downloadfile.h)
smoothmail_include_directories(smoothmail_storage PUBLIC src)
smoothmail_include_directories(smoothmail_storage PUBLIC "${PROJECT_BINARY_DIR}/generated")
smoothmail_link_libraries(smoothmail_storage PUBLIC smoothmail_diagnostics Qt6::Core
    PRIVATE Qt6::Sql Qt6Keychain::Qt6Keychain smoothmail_mime smoothmail_oauth)
smoothmail_target_defaults(smoothmail_storage)
smoothmail_add_library(smoothmail_auth STATIC src/mail/authenticationbroker.cpp src/mail/authenticationbroker.h)
smoothmail_include_directories(smoothmail_auth PUBLIC src)
smoothmail_link_libraries(smoothmail_auth PUBLIC smoothmail_storage smoothmail_oauth)
smoothmail_target_defaults(smoothmail_auth)
smoothmail_add_library(smoothmail_threading STATIC src/core/threading.cpp src/core/threading.h)
smoothmail_include_directories(smoothmail_threading PUBLIC src)
smoothmail_link_libraries(smoothmail_threading PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_threading)
smoothmail_add_library(smoothmail_transport STATIC src/mail/mailservice.cpp src/mail/mailservice.h)
smoothmail_include_directories(smoothmail_transport PUBLIC src)
smoothmail_link_libraries(smoothmail_transport PUBLIC smoothmail_storage
    PRIVATE KPim6::IMAP KPim6::SMTP KF6::I18n Sasl2::Sasl2 smoothmail_mime smoothmail_connection_policy)
smoothmail_target_defaults(smoothmail_transport)
if(SMOOTHMAIL_BUILD_MIME)
    smoothmail_add_library(smoothmail_mime STATIC src/mime/mimeparser.cpp src/mime/mimeparser.h
        src/mime/messagecomposer.cpp src/mime/messagecomposer.h)
    smoothmail_include_directories(smoothmail_mime PUBLIC src)
    smoothmail_link_libraries(smoothmail_mime PUBLIC Qt6::Core PRIVATE KF6::Mime smoothmail_threading)
    smoothmail_target_defaults(smoothmail_mime)
endif()
smoothmail_add_library(smoothmail_ui STATIC
    src/ui/aboutdialog.cpp src/ui/aboutdialog.h
    src/ui/visiblesplitter.h
    src/ui/systemsettingsdialog.cpp src/ui/systemsettingsdialog.h
    src/ui/downloadhistorydialog.cpp src/ui/downloadhistorydialog.h
    src/ui/foldermodel.cpp src/ui/foldermodel.h
    src/ui/composedialog.cpp src/ui/composedialog.h
    src/ui/accountdialog.cpp src/ui/accountdialog.h
    src/core/messagedocument.h
    src/ui/mainwindow.cpp src/ui/mainwindow.h
    src/ui/messageviewer.cpp src/ui/messageviewer.h
    src/ui/safebodyview.cpp src/ui/safebodyview.h)
smoothmail_include_directories(smoothmail_ui PUBLIC src)
smoothmail_link_libraries(smoothmail_ui PUBLIC Qt6::Widgets smoothmail_storage smoothmail_threading)
smoothmail_target_defaults(smoothmail_ui)
smoothmail_add_application(smoothmail WIN32 src/main.cpp src/app/applicationcontroller.cpp src/app/applicationcontroller.h)
smoothmail_link_libraries(smoothmail PRIVATE smoothmail_ui smoothmail_transport smoothmail_oauth smoothmail_auth)
smoothmail_target_defaults(smoothmail)
