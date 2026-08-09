cmake_minimum_required(VERSION 3.22)
project(matron CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(matron SHARED
  jni_bridge.cpp
  core/asset.cpp
  gfx/gfx.cpp
  gfx/renderer.cpp
  game/level.cpp
  game/hud.cpp
  game/story.cpp
  game/game.cpp
  game/game_hud.cpp
  audio/audio.cpp
)

target_compile_options(matron PRIVATE
  -Wall -ffast-math -fno-exceptions -fno-rtti)

find_library(log-lib log)
target_link_libraries(matron
  ${log-lib}
  android
  EGL
  GLESv3
  aaudio
)
