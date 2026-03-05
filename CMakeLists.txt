cmake_minimum_required(VERSION 3.20)
project(sdrpp_spy_glass LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(SDRPP_SOURCE_DIR "" CACHE PATH "Path to an SDR++ source tree")

if(NOT SDRPP_SOURCE_DIR)
    message(FATAL_ERROR "Set SDRPP_SOURCE_DIR to an SDR++ source checkout before building this module.")
endif()

set(SDRPP_CORE_SRC "${SDRPP_SOURCE_DIR}/core/src")

add_library(spy_glass SHARED
    src/main.cpp
)

target_include_directories(spy_glass PRIVATE
    "${SDRPP_CORE_SRC}"
)

target_link_directories(spy_glass PRIVATE
    "${SDRPP_SOURCE_DIR}/build"
    "${SDRPP_SOURCE_DIR}/build/core"
)

target_link_libraries(spy_glass PRIVATE sdrpp_core)

set_target_properties(spy_glass PROPERTIES
    OUTPUT_NAME "spy_glass"
)
