cmake_minimum_required(VERSION 3.25)

project(SmoothMail VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Qt is supplied by the developer. Do not fetch or install a second SDK.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
option(SMOOTHMAIL_BUILD_MIME "Build the KMime mail parser" ON)
option(SMOOTHMAIL_TEST_SASL "Test installed SASL client mechanisms" OFF)
option(SMOOTHMAIL_TEST_KDE_BASE "Test installed KI18n and KService libraries (without KIO)" OFF)
option(SMOOTHMAIL_TEST_KIO "Test the locally patched KIOCore library" OFF)

function(smoothmail_target_defaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(smoothmail_connection_policy STATIC src/mail/connectionpolicy.cpp src/mail/connectionpolicy.h)
target_include_directories(smoothmail_connection_policy PUBLIC src)
target_link_libraries(smoothmail_connection_policy PUBLIC Qt6::Core)
smoothmail_target_defaults(smoothmail_connection_policy)

if(SMOOTHMAIL_BUILD_MIME)
    find_package(KF6Mime 6.31 CONFIG REQUIRED)
    add_library(smoothmail_mime STATIC src/mail/mimeparser.cpp src/mail/mimeparser.h)
    target_include_directories(smoothmail_mime PUBLIC src)
    target_link_libraries(smoothmail_mime PUBLIC Qt6::Core PRIVATE KF6::Mime)
    smoothmail_target_defaults(smoothmail_mime)
endif()

add_library(smoothmail_ui STATIC
    src/core/messagedocument.h
    src/ui/mainwindow.cpp
    src/ui/mainwindow.h
    src/ui/messageviewer.cpp
    src/ui/messageviewer.h
    src/ui/safebodyview.cpp
    src/ui/safebodyview.h
)
target_include_directories(smoothmail_ui PUBLIC src)
target_link_libraries(smoothmail_ui PUBLIC Qt6::Widgets)
smoothmail_target_defaults(smoothmail_ui)

qt_add_executable(smoothmail WIN32 src/main.cpp)
target_link_libraries(smoothmail PRIVATE smoothmail_ui)
smoothmail_target_defaults(smoothmail)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
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
endif()
