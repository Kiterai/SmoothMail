cmake_minimum_required(VERSION 3.25)

project(SmoothMail VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

# Qt is supplied by the developer. Do not fetch or install a second SDK.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

function(smoothmail_target_defaults target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

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
endif()
