cmake_minimum_required(VERSION 3.9)

project(Reheat LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS WebSockets REQUIRED)

add_executable(Reheat
	main.cpp
)

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
target_link_libraries(Reheat PRIVATE Qt::WebSockets)
target_compile_definitions(Reheat PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)