cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(PredictiveBuffer VERSION 4.0.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find GEODE_SDK environment variable!")
endif()

file(TO_CMAKE_PATH "$ENV{GEODE_SDK}" GEODE_SDK_PATH)
add_subdirectory("${GEODE_SDK_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/geode")

setup_geode_mod(${PROJECT_NAME})
