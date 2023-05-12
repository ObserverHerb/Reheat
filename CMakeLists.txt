cmake_minimum_required(VERSION 3.9)

project(Reheat LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory("jsoncpp")

add_executable(Reheat
	main.cpp
)

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
target_include_directories(Reheat PRIVATE $<TARGET_PROPERTY:jsoncpp_lib,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_directories(Reheat PRIVATE $<TARGET_PROPERTY:jsoncpp_lib,LIBRARY_OUTPUT_DIRECTORY>)
target_link_libraries(Reheat PRIVATE winhttp jsoncpp_lib)
target_compile_definitions(Reheat PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)