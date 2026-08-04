cmake_minimum_required(VERSION 3.28...3.30)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(MoonLitClipCore LANGUAGES CXX)
  set(MOONLIT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
else()
  set(MOONLIT_REPO_ROOT "${CMAKE_SOURCE_DIR}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/finders")

find_package(Qt6 REQUIRED COMPONENTS Core Gui)
find_package(FFmpeg REQUIRED COMPONENTS avcodec avutil avformat swscale swresample)

add_library(moonlit-clip-core STATIC)
add_library(MoonLit::ClipCore ALIAS moonlit-clip-core)

target_sources(
  moonlit-clip-core
  PRIVATE
    Clip.cpp
    Clip.hpp
    MoonLitPaths.cpp
    MoonLitPaths.hpp
    capture/CaptureStateMachine.cpp
    capture/CaptureStateMachine.hpp
    capture/CaptureTypes.cpp
    capture/CaptureTypes.hpp
    editor/ClipExportService.cpp
    editor/ClipExportService.hpp
    editor/ExportMath.hpp
    editor/FFmpegPipeline.hpp
    editor/FfmpegTimelineExporter.cpp
    editor/FfmpegTimelineExporter.hpp
    editor/Timeline.cpp
    editor/Timeline.hpp
    media/ClipPreviewService.cpp
    media/ClipPreviewService.hpp
    media/MediaProbe.cpp
    media/MediaProbe.hpp
    media/ThumbnailService.cpp
    media/ThumbnailService.hpp
    output/EncoderResolver.cpp
    output/EncoderResolver.hpp
    persistence/ClipJson.hpp
    persistence/ClipRepository.hpp
    persistence/FileClipRepository.cpp
    persistence/FileClipRepository.hpp
    persistence/JsonImport.cpp
    persistence/JsonImport.hpp
    persistence/SqliteClipRepository.cpp
    persistence/SqliteClipRepository.hpp
    platform/IPlatformServices.hpp
    services/ClipJobs.cpp
    services/ClipJobs.hpp
    services/ExportQueue.cpp
    services/ExportQueue.hpp
)

if(WIN32)
  target_sources(moonlit-clip-core PRIVATE platform/PlatformServices_win.cpp)
else()
  target_sources(moonlit-clip-core PRIVATE platform/PlatformServices_linux.cpp)
endif()

add_subdirectory("${MOONLIT_REPO_ROOT}/deps/sqlite" "${CMAKE_BINARY_DIR}/deps/sqlite")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_subdirectory("${MOONLIT_REPO_ROOT}/test/moonlit" "${CMAKE_BINARY_DIR}/test/moonlit")
endif()

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
    FFmpeg::swresample
  PRIVATE
    sqlite3
)

set_target_properties(moonlit-clip-core PROPERTIES AUTOMOC ON FOLDER frontend)
