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

add_executable(ic-selftest selftest/selftest.cpp ${FW}/src/board/Tm1637.cpp)
target_include_directories(ic-selftest PRIVATE ${FW}/src/board)
target_link_libraries(ic-selftest PRIVATE ic_core ic_simcommon)

add_executable(ic-netsim netsim/netsim.cpp)
target_link_libraries(ic-netsim PRIVATE ic_core)
target_compile_options(ic-netsim PRIVATE -O2)

find_package(Qt6 QUIET COMPONENTS Core Network Widgets)
if(Qt6_FOUND AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp)
    add_executable(intercom-sim WIN32
        app/main.cpp
        app/SimUnit.cpp app/SimUnit.h
        app/SimWindow.cpp app/SimWindow.h
        app/SimWebServer.cpp app/SimWebServer.h
        app/WinAudio.cpp app/WinAudio.h)
    target_link_libraries(intercom-sim PRIVATE ic_core ic_simcommon Qt6::Core Qt6::Network Qt6::Widgets)
    if(WIN32)
        target_link_libraries(intercom-sim PRIVATE winmm)
    endif()

    # Office console: room grid, all-call / talk to a room, signed commands.
    add_executable(intercom-console WIN32
        console/main.cpp
        console/ConsoleWindow.cpp console/ConsoleWindow.h
        app/WinAudio.cpp app/WinAudio.h)
    target_include_directories(intercom-console PRIVATE app)
    target_link_libraries(intercom-console PRIVATE ic_core ic_simcommon Qt6::Core Qt6::Network Qt6::Widgets)
    if(WIN32)
        target_link_libraries(intercom-console PRIVATE winmm)
    endif()
endif()
