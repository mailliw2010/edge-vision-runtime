cmake_minimum_required(VERSION 3.16)
project(edge_vision_runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(runtime-supervisor cmd/runtime-supervisor/main.cpp)
add_executable(runtime-worker cmd/runtime-worker/main.cpp)
