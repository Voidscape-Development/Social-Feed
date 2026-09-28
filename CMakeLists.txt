cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

set(SOCIAL_FEED_TWITCH_CLIENT_ID "" CACHE STRING "Twitch application Client ID used for device-code login")

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "Social Feed requires ENABLE_FRONTEND_API and ENABLE_QT")
endif()

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  # Private QPA header for the Wayland surface handle used by the editor preview.
  # Newer Qt ships it as its own package component; older versions define it with Qt6::Gui.
  find_package(Qt6 QUIET COMPONENTS Gui GuiPrivate)
  if(TARGET Qt6::GuiPrivate)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
  else()
    message(WARNING "Qt6::GuiPrivate not found: the design editor preview will not work on Wayland")
  endif()
endif()

find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SOCIAL_FEED_TWITCH_CLIENT_ID="${SOCIAL_FEED_TWITCH_CLIENT_ID}")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/core/config-store.cpp
    src/core/config-store.hpp
    src/core/event-bus.cpp
    src/core/event-bus.hpp
    src/core/feed-types.cpp
    src/core/feed-types.hpp
    src/core/sample-data.cpp
    src/core/sample-data.hpp
    src/net/http-client.cpp
    src/net/http-client.hpp
    src/net/local-server.cpp
    src/net/local-server.hpp
    src/net/websocket-client.cpp
    src/net/websocket-client.hpp
    src/providers/oauth-device.hpp
    src/providers/provider.hpp
    src/providers/provider-manager.cpp
    src/providers/provider-manager.hpp
    src/providers/stub-providers.cpp
    src/providers/stub-providers.hpp
    src/providers/twitch/twitch-api.cpp
    src/providers/twitch/twitch-api.hpp
    src/providers/twitch/twitch-auth.cpp
    src/providers/twitch/twitch-auth.hpp
    src/providers/twitch/twitch-chat.cpp
    src/providers/twitch/twitch-chat.hpp
    src/providers/twitch/twitch-eventsub.cpp
    src/providers/twitch/twitch-eventsub.hpp
    src/providers/twitch/twitch-provider.cpp
    src/providers/twitch/twitch-provider.hpp
    src/providers/youtube/youtube-api.cpp
    src/providers/youtube/youtube-api.hpp
    src/providers/youtube/youtube-chat-parser.cpp
    src/providers/youtube/youtube-chat-parser.hpp
    src/providers/youtube/youtube-provider.cpp
    src/providers/youtube/youtube-provider.hpp
    src/sources/design-defaults.cpp
    src/sources/design-defaults.hpp
    src/sources/overlay-source.cpp
    src/sources/overlay-source.hpp
    src/sources/source-registry.cpp
    src/sources/source-registry.hpp
    src/ui/chat-editor.cpp
    src/ui/chat-editor.hpp
    src/ui/design-editor.cpp
    src/ui/design-editor.hpp
    src/ui/editor-widgets.cpp
    src/ui/editor-widgets.hpp
    src/ui/event-editor.cpp
    src/ui/event-editor.hpp
    src/ui/preview-widget.cpp
    src/ui/preview-widget.hpp
    src/ui/social-feed-dock.cpp
    src/ui/social-feed-dock.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
