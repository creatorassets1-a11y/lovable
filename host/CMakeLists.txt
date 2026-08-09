# Host-side builds of the shipping engine, against desktop Mesa, so frames can be
# rendered and the game can be smoke-tested without a device.
cmake_minimum_required(VERSION 3.16)
project(matron_host CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(CPP ${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/cpp)

set(ENGINE_SRC
  ${CPP}/core/asset.cpp
  ${CPP}/gfx/gfx.cpp
  ${CPP}/gfx/renderer.cpp
  ${CPP}/game/level.cpp
)
set(GAME_SRC
  ${CPP}/game/hud.cpp
  ${CPP}/game/story.cpp
  ${CPP}/game/game.cpp
  ${CPP}/game/game_hud.cpp
  ${CPP}/audio/audio.cpp
)

add_executable(matron_preview main_host.cpp stb_write_shim.cpp ${ENGINE_SRC})
target_include_directories(matron_preview PRIVATE ${CPP})
target_link_libraries(matron_preview EGL GLESv2 m)

add_executable(matron_game main_game.cpp stb_write_shim.cpp ${ENGINE_SRC} ${GAME_SRC})
target_include_directories(matron_game PRIVATE ${CPP})
target_link_libraries(matron_game EGL GLESv2 m pthread)
