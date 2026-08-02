cmake_minimum_required(VERSION 3.28...3.30)

if(NOT TARGET OBS::obfuscate)
  add_subdirectory("${CMAKE_SOURCE_DIR}/libobs" "${CMAKE_BINARY_DIR}/libobs")
endif()

find_package(jansson REQUIRED)

add_library(win-capture MODULE)
add_library(OBS::capture ALIAS win-capture)

target_sources(
  win-capture
  PRIVATE
    audio-helpers.c
    audio-helpers.h
    compat-format-ver.h
    compat-helpers.c
    compat-helpers.h
    cursor-capture.c
    cursor-capture.h
    dc-capture.c
    dc-capture.h
    duplicator-monitor-capture.c
    monitor-capture.c
    plugin-main.c
    window-capture.c
)

target_compile_definitions(
  win-capture
  PRIVATE
    OBS_VERSION="${OBS_VERSION_CANONICAL}"
)

target_link_libraries(
  win-capture
  PRIVATE
    OBS::libobs
    OBS::w32-pthreads
    OBS::winrt-headers
    jansson::jansson
)

# Remove once jansson has been fixed on obs-deps
target_link_options(win-capture PRIVATE /IGNORE:4098)

configure_file(cmake/windows/obs-module.rc.in win-capture.rc)
target_sources(win-capture PRIVATE win-capture.rc)

set_target_properties_obs(win-capture PROPERTIES FOLDER plugins/win-capture PREFIX "")
