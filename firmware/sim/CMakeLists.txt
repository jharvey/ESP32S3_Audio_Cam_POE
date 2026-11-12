cmake_minimum_required(VERSION 3.21)
project(IntercomSim LANGUAGES CXX)

# Classroom intercom desktop simulator. Compiles the real firmware core
# (firmware/src/core, unchanged) and runs it on simulated hardware: PC audio or a
# synthetic talker, simulated room echo, UDP multicast as the network, PoE / Wi-Fi /
# battery switches, and the front panel drawn on screen.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

get_filename_component(FW ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

file(GLOB CORE_SRCS CONFIGURE_DEPENDS ${FW}/src/core/*.cpp)
add_library(ic_core STATIC ${CORE_SRCS})
target_include_directories(ic_core PUBLIC ${FW}/src/core)
target_compile_options(ic_core PRIVATE -O2 -Wall -Wextra)

add_library(ic_simcommon STATIC common/RoomEcho.cpp common/TestVoice.cpp)
target_include_directories(ic_simcommon PUBLIC common)
target_compile_options(ic_simcommon PRIVATE -O2 -Wall)

add_executable(ic-selftest selftest/selftest.cpp)
target_link_libraries(ic-selftest PRIVATE ic_core ic_simcommon)

find_package(Qt6 QUIET COMPONENTS Core Network Widgets)
if(Qt6_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp)
    add_executable(intercom-sim WIN32
        app/main.cpp
        app/SimUnit.cpp app/SimUnit.h
        app/SimWindow.cpp app/SimWindow.h
        app/WinAudio.cpp app/WinAudio.h)
    target_link_libraries(intercom-sim PRIVATE ic_core ic_simcommon Qt6::Core Qt6::Network Qt6::Widgets)
    if(WIN32)
        target_link_libraries(intercom-sim PRIVATE winmm)
    endif()
endif()
