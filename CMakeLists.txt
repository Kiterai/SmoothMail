cmake_minimum_required(VERSION 3.25)

project(SmoothMail VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
option(SMOOTHMAIL_DISTRIBUTION "Build a privacy-hardened Windows Release application" OFF)
if(SMOOTHMAIL_DISTRIBUTION)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR "Distribution requires Windows/MSVC Release")
    endif()
    # Avoid embedding the builder's source path in __FILE__ and compiler records.
    add_compile_options("/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=SmoothMail")
    add_link_options("/PDBALTPATH:smoothmail.pdb" "/INCREMENTAL:NO" "/Brepro")
endif()

# Qt is supplied by the developer. Do not fetch or install a second SDK.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Sql Network)
find_package(Qt6Keychain CONFIG REQUIRED)
find_package(ECM 6.31 CONFIG REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
find_package(Sasl2 REQUIRED)
find_package(KPim6IMAP 6.8.3 CONFIG REQUIRED)
find_package(KPim6SMTP 6.8.3 CONFIG REQUIRED)
find_package(KF6KIO 6.31 CONFIG REQUIRED)
find_package(KF6Mime 6.31 CONFIG REQUIRED)
find_package(KF6Codecs 6.31 CONFIG REQUIRED)
include(cmake/ValidateMailDependencies.cmake)
set(SMOOTHMAIL_SASL_PLUGIN_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/install/${CMAKE_BUILD_TYPE}/bin/sasl2"
    CACHE PATH "SASL mechanisms to test (never a global plugin path)")
option(SMOOTHMAIL_BUILD_MIME "Build the KMime mail parser" ON)
if(NOT SMOOTHMAIL_BUILD_MIME)
    message(FATAL_ERROR "The application storage and mail service require SMOOTHMAIL_BUILD_MIME=ON.")
endif()
option(SMOOTHMAIL_TEST_SASL "Test installed SASL client mechanisms" OFF)
option(SMOOTHMAIL_TEST_KDE_BASE "Test installed KI18n and KService libraries (without KIO)" OFF)
option(SMOOTHMAIL_TEST_KIO "Test the locally patched KIOCore library" OFF)
option(SMOOTHMAIL_TEST_MAIL "Test KIMAP/KSMTP against synthetic loopback servers" OFF)

function(smoothmail_target_defaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(smoothmail_diagnostics STATIC src/core/diagnostics.cpp src/core/diagnostics.h)
target_include_directories(smoothmail_diagnostics PUBLIC src)
target_link_libraries(smoothmail_diagnostics PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_diagnostics)
add_library(smoothmail_connection_policy STATIC src/mail/connectionpolicy.cpp src/mail/connectionpolicy.h)
target_include_directories(smoothmail_connection_policy PUBLIC src)
target_link_libraries(smoothmail_connection_policy PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_connection_policy)
add_library(smoothmail_oauth STATIC src/mail/oauthclient.cpp src/mail/oauthclient.h)
target_include_directories(smoothmail_oauth PUBLIC src)
target_link_libraries(smoothmail_oauth PUBLIC Qt6::Core Qt6::Network)
smoothmail_target_defaults(smoothmail_oauth)

add_library(smoothmail_storage STATIC src/core/account.cpp src/core/account.h
    src/storage/accountrepository.cpp src/storage/accountrepository.h
    src/storage/accountregistrar.cpp src/storage/accountregistrar.h
    src/storage/credentialstore.cpp src/storage/credentialstore.h)
target_sources(smoothmail_storage PRIVATE src/storage/downloadfile.cpp src/storage/downloadfile.h)
target_include_directories(smoothmail_storage PUBLIC src)
target_link_libraries(smoothmail_storage PUBLIC smoothmail_connection_policy smoothmail_diagnostics Qt6::Core
    PRIVATE Qt6::Sql Qt6Keychain::Qt6Keychain smoothmail_mime smoothmail_oauth)
smoothmail_target_defaults(smoothmail_storage)
add_library(smoothmail_auth STATIC src/mail/authenticationbroker.cpp src/mail/authenticationbroker.h)
target_include_directories(smoothmail_auth PUBLIC src)
target_link_libraries(smoothmail_auth PUBLIC smoothmail_storage smoothmail_oauth)
smoothmail_target_defaults(smoothmail_auth)
add_library(smoothmail_threading STATIC src/core/threading.cpp src/core/threading.h)
target_include_directories(smoothmail_threading PUBLIC src)
target_link_libraries(smoothmail_threading PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_threading)

add_library(smoothmail_transport STATIC src/mail/mailservice.cpp src/mail/mailservice.h)
target_include_directories(smoothmail_transport PUBLIC src)
target_link_libraries(smoothmail_transport PUBLIC smoothmail_storage
    PRIVATE KPim6::IMAP KPim6::SMTP KF6::I18n Sasl2::Sasl2 smoothmail_mime)
smoothmail_target_defaults(smoothmail_transport)

if(SMOOTHMAIL_BUILD_MIME)
    find_package(KF6Mime 6.31 CONFIG REQUIRED)
    add_library(smoothmail_mime STATIC src/mail/mimeparser.cpp src/mail/mimeparser.h
        src/mail/messagecomposer.cpp src/mail/messagecomposer.h)
    target_include_directories(smoothmail_mime PUBLIC src)
    target_link_libraries(smoothmail_mime PUBLIC Qt6::Core PRIVATE KF6::Mime smoothmail_threading)
    smoothmail_target_defaults(smoothmail_mime)
endif()

add_library(smoothmail_ui STATIC
    src/ui/visiblesplitter.h
    src/ui/systemsettingsdialog.cpp src/ui/systemsettingsdialog.h
    src/ui/downloadhistorydialog.cpp src/ui/downloadhistorydialog.h
    src/ui/foldermodel.cpp src/ui/foldermodel.h
    src/ui/composedialog.cpp src/ui/composedialog.h
    src/ui/accountdialog.cpp src/ui/accountdialog.h
    src/core/messagedocument.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/messageviewer.cpp
    src/ui/messageviewer.h
    src/ui/safebodyview.cpp
    src/ui/safebodyview.h
)
target_include_directories(smoothmail_ui PUBLIC src)
target_link_libraries(smoothmail_ui PUBLIC Qt6::Widgets smoothmail_storage smoothmail_threading)
smoothmail_target_defaults(smoothmail_ui)

qt_add_executable(smoothmail WIN32 src/main.cpp src/app/applicationcontroller.cpp src/app/applicationcontroller.h)
target_link_libraries(smoothmail PRIVATE smoothmail_ui smoothmail_transport smoothmail_oauth smoothmail_auth)
smoothmail_target_defaults(smoothmail)

option(SMOOTHMAIL_BUILD_LIVE_GOOGLE_PROBE "Build opt-in live Google OAuth/authentication probe (never a CTest test)" OFF)
if(SMOOTHMAIL_BUILD_LIVE_GOOGLE_PROBE)
    add_executable(smoothmail_google_probe tests/manual/google_oauth_probe.cpp
        tests/manual/google_credential_probe.cpp tests/manual/google_credential_probe.h)
    target_link_libraries(smoothmail_google_probe PRIVATE smoothmail_transport smoothmail_oauth smoothmail_auth Qt6::Gui)
    smoothmail_target_defaults(smoothmail_google_probe)
endif()

include(CTest)
option(SMOOTHMAIL_BUILD_GUI_SMOKE "Build explicit native GUI workflow test for disposable Windows Sandbox only" OFF)
if(SMOOTHMAIL_BUILD_GUI_SMOKE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_guismoke tests/tst_guismoke.cpp tests/mailtestserver.h
        src/app/applicationcontroller.cpp src/app/applicationcontroller.h)
    target_link_libraries(tst_guismoke PRIVATE smoothmail_ui smoothmail_transport smoothmail_auth Qt6::Test Qt6::Network)
    smoothmail_target_defaults(tst_guismoke)
    qt_add_resources(tst_guismoke "gui_test_tls" PREFIX "/test"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/tests/data" FILES tests/data/tls-cert.pem tests/data/tls-key.pem)
    # Deliberately not registered with CTest: dummy OS credentials/downloads
    # and visible native windows are allowed only in the disposable guest.
endif()
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_diagnostics tests/tst_diagnostics.cpp)
    target_link_libraries(tst_diagnostics PRIVATE smoothmail_diagnostics Qt6::Test)
    smoothmail_target_defaults(tst_diagnostics)
    add_test(NAME diagnostics COMMAND tst_diagnostics)
    add_executable(tst_mimeregression tests/tst_mimeregression.cpp)
    target_link_libraries(tst_mimeregression PRIVATE smoothmail_mime smoothmail_diagnostics Qt6::Test)
    smoothmail_target_defaults(tst_mimeregression)
    add_test(NAME mimeregression COMMAND tst_mimeregression)
    if(SMOOTHMAIL_BUILD_LIVE_GOOGLE_PROBE)
        add_test(NAME google_probe_lifecycle COMMAND smoothmail_google_probe --self-test-lifecycle)
        set_tests_properties(google_probe_lifecycle PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    add_executable(tst_threading tests/tst_threading.cpp)
    target_link_libraries(tst_threading PRIVATE smoothmail_threading Qt6::Test)
    smoothmail_target_defaults(tst_threading)
    add_test(NAME threading COMMAND tst_threading)
    add_executable(tst_accountdialog tests/tst_accountdialog.cpp)
    target_link_libraries(tst_accountdialog PRIVATE smoothmail_ui Qt6::Test)
    smoothmail_target_defaults(tst_accountdialog)
    add_test(NAME accountdialog COMMAND tst_accountdialog)
    set_tests_properties(accountdialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    foreach(name IN ITEMS accountrepository credentialstore)
        add_executable(tst_${name} tests/tst_${name}.cpp)
        target_link_libraries(tst_${name} PRIVATE smoothmail_storage Qt6::Test)
        if(name STREQUAL "accountrepository")
            target_link_libraries(tst_${name} PRIVATE smoothmail_mime Qt6::Sql)
        endif()
        smoothmail_target_defaults(tst_${name})
        add_test(NAME ${name} COMMAND tst_${name})
    endforeach()
    add_executable(tst_connectionpolicy tests/tst_connectionpolicy.cpp)
    target_link_libraries(tst_connectionpolicy PRIVATE smoothmail_connection_policy Qt6::Test)
    smoothmail_target_defaults(tst_connectionpolicy)
    add_test(NAME connectionpolicy COMMAND tst_connectionpolicy)
    add_executable(tst_mainwindow tests/tst_mainwindow.cpp)
    target_link_libraries(tst_mainwindow PRIVATE smoothmail_ui Qt6::Test)
    smoothmail_target_defaults(tst_mainwindow)
    add_test(NAME mainwindow COMMAND tst_mainwindow)
    set_tests_properties(mainwindow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(tst_messageviewer tests/tst_messageviewer.cpp)
    target_link_libraries(tst_messageviewer PRIVATE smoothmail_ui Qt6::Test)
    smoothmail_target_defaults(tst_messageviewer)
    add_test(NAME messageviewer COMMAND tst_messageviewer)
    set_tests_properties(messageviewer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    if(SMOOTHMAIL_BUILD_MIME)
        add_executable(tst_messagecomposer tests/tst_messagecomposer.cpp)
        target_link_libraries(tst_messagecomposer PRIVATE smoothmail_mime smoothmail_threading Qt6::Test)
        smoothmail_target_defaults(tst_messagecomposer)
        add_test(NAME messagecomposer COMMAND tst_messagecomposer)
        add_executable(tst_mimeparser tests/tst_mimeparser.cpp)
        target_link_libraries(tst_mimeparser PRIVATE smoothmail_mime Qt6::Test)
        smoothmail_target_defaults(tst_mimeparser)
        add_test(NAME mimeparser COMMAND tst_mimeparser)
    endif()

    if(SMOOTHMAIL_TEST_SASL)
        find_package(ECM 6.31 CONFIG REQUIRED)
        list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
        find_package(Sasl2 REQUIRED)
        set(SMOOTHMAIL_SASL_PLUGIN_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/install/${CMAKE_BUILD_TYPE}/bin/sasl2"
            CACHE PATH "SASL mechanisms to test (never a global plugin path)")
        add_executable(tst_sasl tests/tst_sasl.cpp)
        target_link_libraries(tst_sasl PRIVATE Qt6::Test Sasl2::Sasl2)
        if(WIN32)
            target_link_libraries(tst_sasl PRIVATE ws2_32)
        endif()
        smoothmail_target_defaults(tst_sasl)
        add_test(NAME sasl COMMAND tst_sasl)
        set_tests_properties(sasl PROPERTIES
            ENVIRONMENT "SMOOTHMAIL_TEST_SASL_PATH=${SMOOTHMAIL_SASL_PLUGIN_DIR}")
    endif()

    if(SMOOTHMAIL_TEST_KDE_BASE)
        find_package(KF6I18n 6.31 CONFIG REQUIRED)
        find_package(KF6Service 6.31 CONFIG REQUIRED)
        add_executable(tst_kdebase tests/tst_kdebase.cpp)
        target_link_libraries(tst_kdebase PRIVATE Qt6::Test KF6::I18n KF6::Service)
        smoothmail_target_defaults(tst_kdebase)
        add_test(NAME kdebase COMMAND tst_kdebase)
    endif()

    if(SMOOTHMAIL_TEST_KIO)
        find_package(KF6KIO 6.31 CONFIG REQUIRED)
        add_executable(tst_kio tests/tst_kio.cpp)
        target_link_libraries(tst_kio PRIVATE Qt6::Test KF6::KIOCore)
        smoothmail_target_defaults(tst_kio)
        add_test(NAME kio COMMAND tst_kio)
    endif()

    if(SMOOTHMAIL_TEST_MAIL)
        add_executable(tst_oauthclient tests/tst_oauthclient.cpp)
        target_link_libraries(tst_oauthclient PRIVATE smoothmail_oauth smoothmail_auth Qt6::Test)
        smoothmail_target_defaults(tst_oauthclient)
        add_test(NAME oauthclient COMMAND tst_oauthclient)
        set_tests_properties(oauthclient PROPERTIES TIMEOUT 60)
        add_executable(tst_connectiontester tests/tst_connectiontester.cpp tests/mailtestserver.h)
        qt_add_resources(tst_connectiontester "test_tls" PREFIX "/test"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
            FILES tests/data/tls-cert.pem tests/data/tls-key.pem)
        target_link_libraries(tst_connectiontester PRIVATE smoothmail_transport Qt6::Test Qt6::Network)
        smoothmail_target_defaults(tst_connectiontester)
        add_test(NAME connectiontester COMMAND tst_connectiontester)
        set_tests_properties(connectiontester PROPERTIES TIMEOUT 120
            ENVIRONMENT "SMOOTHMAIL_TEST_SASL_PATH=${SMOOTHMAIL_SASL_PLUGIN_DIR}")
        find_package(ECM 6.31 CONFIG REQUIRED)
        list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
        find_package(Sasl2 REQUIRED)
        find_package(KPim6IMAP 6.8.3 CONFIG REQUIRED)
        find_package(KPim6SMTP 6.8.3 CONFIG REQUIRED)
        set(SMOOTHMAIL_SASL_PLUGIN_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/install/${CMAKE_BUILD_TYPE}/bin/sasl2"
            CACHE PATH "SASL mechanisms to test (never a global plugin path)")
        add_executable(tst_mailtransport tests/tst_mailtransport.cpp tests/mailtestserver.h)
        target_link_libraries(tst_mailtransport PRIVATE smoothmail_connection_policy Qt6::Test
            KPim6::IMAP KPim6::SMTP KF6::I18n Sasl2::Sasl2)
        smoothmail_target_defaults(tst_mailtransport)
        add_test(NAME mailtransport COMMAND tst_mailtransport)
        set_tests_properties(mailtransport PROPERTIES TIMEOUT 90
            ENVIRONMENT "SMOOTHMAIL_TEST_SASL_PATH=${SMOOTHMAIL_SASL_PLUGIN_DIR}")
    endif()
endif()
