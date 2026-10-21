cmake_minimum_required(VERSION 3.21)
project(ytmusic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)

add_executable(ytmusic
    src/main.cpp
    src/theme.cpp
    src/icons.cpp
    src/thumbs.cpp
    src/core.cpp
    src/mpris.cpp
    src/ui_common.cpp
    src/tracklist.cpp
    src/lyrics.cpp
    src/playerbar.cpp
    src/mainwindow.cpp
)
target_link_libraries(ytmusic PRIVATE Qt6::Widgets Qt6::DBus)
target_compile_definitions(ytmusic PRIVATE QT_NO_CAST_TO_ASCII QT_NO_FOREACH)
target_compile_options(ytmusic PRIVATE -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
target_link_options(ytmusic PRIVATE -Wl,--gc-sections -Wl,--as-needed -Wl,-O1 -s)
set_property(TARGET ytmusic PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)

install(TARGETS ytmusic RUNTIME DESTINATION bin)
