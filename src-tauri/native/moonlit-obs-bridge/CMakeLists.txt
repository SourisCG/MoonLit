cmake_minimum_required(VERSION 3.24)

project(moonlit-obs-bridge LANGUAGES CXX)

if(NOT DEFINED LIBOBS_BUILD_ROOT)
  message(FATAL_ERROR "LIBOBS_BUILD_ROOT must point at the pinned libobs build")
endif()

add_library(moonlit-obs-bridge SHARED src/bridge.cpp)
target_compile_features(moonlit-obs-bridge PRIVATE cxx_std_17)
target_include_directories(moonlit-obs-bridge PRIVATE
  "${LIBOBS_BUILD_ROOT}/libobs"
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_directories(moonlit-obs-bridge PRIVATE "${LIBOBS_BUILD_ROOT}/libobs")
target_link_libraries(moonlit-obs-bridge PRIVATE obs)
set_target_properties(moonlit-obs-bridge PROPERTIES
  OUTPUT_NAME "moonlit-obs-bridge"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/64bit"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/64bit"
)
