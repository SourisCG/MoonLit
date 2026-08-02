cmake_minimum_required(VERSION 3.28...3.30)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(MoonLitClipCore LANGUAGES CXX)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/finders")

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat swscale)

add_library(moonlit-clip-core STATIC)
add_library(MoonLit::ClipCore ALIAS moonlit-clip-core)

target_sources(
  moonlit-clip-core
  PRIVATE
    Clip.cpp
    Clip.hpp
    MoonLitPaths.cpp
    MoonLitPaths.hpp
    editor/ClipExportService.cpp
    editor/ClipExportService.hpp
    media/MediaProbe.cpp
    media/MediaProbe.hpp
    media/ThumbnailService.cpp
    media/ThumbnailService.hpp
    output/EncoderResolver.cpp
    output/EncoderResolver.hpp
    persistence/ClipRepository.hpp
    persistence/FileClipRepository.cpp
    persistence/FileClipRepository.hpp
)

target_include_directories(moonlit-clip-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_compile_features(moonlit-clip-core PUBLIC cxx_std_17)

target_link_libraries(
  moonlit-clip-core
  PUBLIC
    Qt::Core
    Qt::Gui
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
)

set_target_properties(moonlit-clip-core PROPERTIES FOLDER frontend)
