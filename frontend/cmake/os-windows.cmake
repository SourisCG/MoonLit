option(ENABLE_UPDATER "Enable the Windows updater" ON)

if(NOT TARGET OBS::blake2 AND (ENABLE_UPDATER OR ENABLE_WHATSNEW))
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/blake2" "${CMAKE_BINARY_DIR}/deps/blake2")
endif()

if(NOT TARGET OBS::w32-pthreads)
  add_subdirectory("${CMAKE_SOURCE_DIR}/deps/w32-pthreads" "${CMAKE_BINARY_DIR}/deps/w32-pthreads")
endif()

if(ENABLE_UPDATER OR ENABLE_WHATSNEW)
  set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
  find_package(MbedTLS REQUIRED)
  set(CMAKE_FIND_PACKAGE_PREFER_CONFIG FALSE)
endif()
if(NOT MOONLIT_BUILD)
  find_package(Detours REQUIRED)
endif()
find_package(nlohmann_json 3.11 REQUIRED)

if(MOONLIT_BUILD)
  set(OBS_WINDOWS_ICON "cmake/windows/MoonLit.ico")
  set(OBS_EXE_NAME "MoonLit")
else()
  set(OBS_WINDOWS_ICON "cmake/windows/obs-studio.ico")
  set(OBS_EXE_NAME "obs")
endif()

configure_file(cmake/windows/obs.rc.in obs.rc)
configure_file(cmake/windows/obs.manifest "${CMAKE_CURRENT_BINARY_DIR}/obs.manifest")

target_sources(
  obs-studio
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/obs.manifest"
    obs.rc
    utility/CrashHandler_Windows.cpp
    utility/NativeEventFilter_Windows.cpp
    utility/platform-windows.cpp
    utility/system-info-windows.cpp
    $<$<NOT:$<BOOL:${MOONLIT_BUILD}>>:utility/win-dll-blocklist.c>
)

if(ENABLE_UPDATER OR ENABLE_WHATSNEW)
  target_sources(
    obs-studio
    PRIVATE
      utility/WhatsNewInfoThread.cpp
      utility/WhatsNewInfoThread.hpp
      utility/crypto-helpers-mbedtls.cpp
      utility/crypto-helpers.hpp
      utility/update-helpers.cpp
      utility/update-helpers.hpp
  )
endif()

if(ENABLE_WHATSNEW)
  target_sources(
    obs-studio
    PRIVATE
      utility/WhatsNewBrowserInitThread.cpp
      utility/WhatsNewBrowserInitThread.hpp
      utility/models/whatsnew.hpp
  )
endif()

if(ENABLE_UPDATER)
  target_sources(
    obs-studio
    PRIVATE
      dialogs/OBSUpdate.cpp
      dialogs/OBSUpdate.hpp
      forms/OBSUpdate.ui
      utility/AutoUpdateThread.cpp
      utility/AutoUpdateThread.hpp
      utility/models/branches.hpp
  )
endif()

if(ENABLE_UPDATER)
  add_library(obs-updater-manifest INTERFACE)
  add_library(OBS::updater-manifest ALIAS obs-updater-manifest)
  target_sources(obs-updater-manifest INTERFACE updater/manifest.hpp)
endif()

target_link_libraries(
  obs-studio
  PRIVATE
    crypt32
    OBS::w32-pthreads
    nlohmann_json::nlohmann_json
)

if(NOT MOONLIT_BUILD)
  target_link_libraries(obs-studio PRIVATE Detours::Detours)
endif()

if(ENABLE_UPDATER OR ENABLE_WHATSNEW)
  target_link_libraries(obs-studio PRIVATE OBS::blake2 MbedTLS::mbedtls)
endif()

if(ENABLE_UPDATER)
  target_link_libraries(obs-studio PRIVATE OBS::updater-manifest)
  target_enable_feature(obs-studio "Windows updater" UPDATER_ENABLED)
else()
  target_disable_feature(obs-studio "Windows updater")
endif()

target_compile_definitions(obs-studio PRIVATE PSAPI_VERSION=2)

target_link_options(obs-studio PRIVATE /IGNORE:4099 $<$<CONFIG:DEBUG>:/NODEFAULTLIB:MSVCRT>)

if(ENABLE_UPDATER)
  # Set commit for untagged version comparisons in the Windows updater.
  if(OBS_VERSION MATCHES ".+g[a-f0-9]+.*")
    string(REGEX REPLACE ".+g([a-f0-9]+).*$" "\\1" OBS_COMMIT ${OBS_VERSION})
  else()
    set(OBS_COMMIT "")
  endif()

  set_source_files_properties(utility/AutoUpdateThread.cpp PROPERTIES COMPILE_DEFINITIONS OBS_COMMIT="${OBS_COMMIT}")
  add_subdirectory(updater)
endif()

set_property(TARGET obs-studio APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT obs-studio)
set_target_properties(
  obs-studio
  PROPERTIES
    WIN32_EXECUTABLE TRUE
    # TARGET_FILE_NAME follows the product-specific output name (MoonLit or OBS).
    VS_DEBUGGER_COMMAND "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit/$<TARGET_FILE_NAME:obs-studio>"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/rundir/$<CONFIG>/bin/64bit"
)
