cmake_minimum_required(VERSION 3.19)
project(steel_knuckle CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../third_party" CACHE PATH
    "Directory holding DiligentCore and the SDL3 development package")

# -----------------------------------------------------------------------------
# Diligent Engine
#
# This toolchain is MinGW, so the Direct3D backends are unavailable; the port
# targets the OpenGL and Vulkan backends, which Diligent builds from source.
# -----------------------------------------------------------------------------
set(DILIGENT_NO_DIRECT3D11 ON  CACHE BOOL "" FORCE)
set(DILIGENT_NO_DIRECT3D12 ON  CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_CORE_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DILIGENT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory("${THIRD_PARTY}/DiligentCore" DiligentCore EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# SDL3 (prebuilt MinGW development package)
# -----------------------------------------------------------------------------
set(SDL3_ROOT "${THIRD_PARTY}/SDL3-3.4.16/x86_64-w64-mingw32")
if(NOT EXISTS "${SDL3_ROOT}/include/SDL3/SDL.h")
    message(FATAL_ERROR "SDL3 not found under ${SDL3_ROOT}")
endif()

add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_LOCATION "${SDL3_ROOT}/bin/SDL3.dll"
    IMPORTED_IMPLIB   "${SDL3_ROOT}/lib/libSDL3.dll.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL3_ROOT}/include")

# -----------------------------------------------------------------------------
# The game
# -----------------------------------------------------------------------------
add_executable(steel_knuckle
    src/main.cpp
    src/render.cpp
    src/meshgen.cpp
    src/font.cpp
    src/platform.cpp
    src/audio.cpp
    src/game.cpp
    src/ui.cpp
    src/scene.cpp
    src/character.cpp
    "${THIRD_PARTY}/ufbx/ufbx.c"
)

# Optional side-by-side output for development while an older build is running.
set(STEEL_KNUCKLE_OUTPUT_DIR "" CACHE PATH "Optional game output directory")
if(STEEL_KNUCKLE_OUTPUT_DIR)
    set_target_properties(steel_knuckle PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${STEEL_KNUCKLE_OUTPUT_DIR}")
endif()

target_include_directories(steel_knuckle PRIVATE src "${THIRD_PARTY}/ufbx" "${THIRD_PARTY}/stb")

target_link_libraries(steel_knuckle PRIVATE
    sdl3
    gdi32
    Diligent-GraphicsEngineOpenGL-static
    Diligent-GraphicsEngineVk-static
    Diligent-GraphicsTools
    Diligent-Common
    Diligent-GraphicsAccessories
)

# Diligent's own guidance: GCC 9+ miscompiles parts of the engine at -O2/-O3.
# The engine build already drops to -O1; match it here for our translation units
# that inline engine headers.
target_compile_options(steel_knuckle PRIVATE $<$<CONFIG:Release>:-O1>)

# Ship SDL3.dll next to the executable.
add_custom_command(TARGET steel_knuckle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_ROOT}/bin/SDL3.dll" "$<TARGET_FILE_DIR:steel_knuckle>")

# Stage the original models and the anime atlas beside the executable. This also
# runs when assets change without recompiling C++ and supports any working dir.
add_custom_target(steel_knuckle_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:steel_knuckle>/assets/91-anime_character"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/91-anime_character/Anime_character.fbx"
        "${CMAKE_CURRENT_SOURCE_DIR}/91-anime_character/textures.png"
        "$<TARGET_FILE_DIR:steel_knuckle>/assets/91-anime_character"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/RITUAL+WOMEN+500k.fbx"
        "$<TARGET_FILE_DIR:steel_knuckle>/assets"
)
add_dependencies(steel_knuckle steel_knuckle_assets)
