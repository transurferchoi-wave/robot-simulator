cmake_minimum_required(VERSION 3.16)
project(RobotSimulator VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O2 -Wall)
endif()

find_package(Threads REQUIRED)

set(SOURCES
    src/main.cpp
    src/Grid.cpp
    src/Robot.cpp
    src/Pathfinder.cpp
    src/RobotController.cpp
    src/ReservationTable.cpp
    src/MessageBus.cpp
    src/EventLogger.cpp
    src/TcpServer.cpp
    src/WebServer.cpp
    src/Simulator.cpp
)

add_executable(robot_sim ${SOURCES})

target_include_directories(robot_sim PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(robot_sim PRIVATE Threads::Threads)

if(APPLE)
    target_compile_definitions(robot_sim PRIVATE PLATFORM_MACOS)
endif()

message(STATUS "RobotSimulator v2.0 | Build: ${CMAKE_BUILD_TYPE}")
