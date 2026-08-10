cmake_minimum_required(VERSION 3.22.1)
project(hollowsignal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NativeActivity glue ships with the NDK; build it as part of the project.
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(hollowsignal SHARED
    main.cpp
    game.cpp
    gfx.cpp
    world.cpp
    actors.cpp
    missions.cpp
    player.cpp
    audio.cpp
    speech.cpp
    assets.cpp
    jnibridge.cpp
    pak_verify.c)

target_compile_options(hollowsignal PRIVATE
    -Wall
    -ffast-math
    -fvisibility=hidden)

target_link_libraries(hollowsignal
    native_app_glue
    android
    log
    EGL
    GLESv3
    OpenSLES
    m)

# The loader looks up ANativeActivity_onCreate by name; without this the
# linker garbage-collects it and the app dies at startup with no message.
set_target_properties(hollowsignal PROPERTIES
    LINK_FLAGS "-u ANativeActivity_onCreate")
