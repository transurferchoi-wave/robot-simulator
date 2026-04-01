cmake_minimum_required(VERSION 3.16)
project(RobotSimulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 컴파일 옵션
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O2 -Wall)
endif()

# macOS에서 pthread
find_package(Threads REQUIRED)

# 소스 파일
set(SOURCES
    src/main.cpp
    src/Grid.cpp
    src/Robot.cpp
    src/Pathfinder.cpp
    src/RobotController.cpp
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

# macOS에서 추가 링크 옵션 없음 (POSIX 소켓은 기본 포함)
if(APPLE)
    target_compile_definitions(robot_sim PRIVATE PLATFORM_MACOS)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
