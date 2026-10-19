cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

# Widgets for the "Téléphones" dock and the pairing window, Network for the phones' control channel.
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

# mDNS announcement ("_obsmobile._tcp"): Windows' own DNS-SD API, Avahi on Linux.
if(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/mdns-announcer-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE dnsapi)
elseif(OS_LINUX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AVAHI REQUIRED IMPORTED_TARGET avahi-client)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/mdns-announcer-avahi.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::AVAHI)
else()
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/mdns-announcer-none.cpp)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/control-server.cpp
    src/device-store.cpp
    src/phones-dock.cpp
    src/pairing-dialog.cpp
    src/phone-camera.cpp
    src/mobile-camera-source.cpp
    src/program-return.cpp
    src/blur-backdrop.cpp
)

# FFmpeg (with SRT) decodes the phones' streams: OBS's own copy on Windows (obs-deps), the
# system's on Linux.
if(OS_LINUX)
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE PkgConfig::FFMPEG)
else()
  find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h REQUIRED)
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIR})
  foreach(_lib avformat avcodec avutil)
    find_library(FFMPEG_${_lib}_LIBRARY ${_lib} REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${FFMPEG_${_lib}_LIBRARY})
  endforeach()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
