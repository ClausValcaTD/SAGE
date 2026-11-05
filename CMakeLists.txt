cmake_minimum_required(VERSION 3.20)

project(SAGE
    VERSION 0.1.0
    DESCRIPTION "Sega Arcade Golden-era Emulator"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# miniz: single-file ZIP reader used to unpack the ROM archives.
set(MINIZ_STANDALONE_PROJECT OFF CACHE INTERNAL "")
FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.1.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(miniz)

# SDL2 provides the display window. It is optional: when it is not found the
# display module compiles to no-ops and the emulator runs headless.
# `SAGE_SDL2_ROOT` lets a self-contained SDL2 tree be pointed at without
# polluting the system prefix (e.g. -DSAGE_SDL2_ROOT=/opt/sdl2/usr).
if(SAGE_SDL2_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${SAGE_SDL2_ROOT}")
    set(SDL2_ROOT "${SAGE_SDL2_ROOT}" CACHE PATH "SDL2 install prefix")
endif()
find_package(SDL2 CONFIG QUIET)

add_executable(sage
    src/main.cpp
    src/display.cpp
    src/rom_loader.cpp
    src/bus.cpp
    src/eeprom.cpp
    src/io.cpp
    src/protection.cpp
    src/video.cpp
    src/cpu/i960.cpp
    src/cpu/i960dis.cpp)

target_include_directories(sage PRIVATE include)
target_link_libraries(sage PRIVATE miniz)

if(SDL2_FOUND)
    target_link_libraries(sage PRIVATE SDL2::SDL2)
    # Debian's SDL_config.h forwards to an arch-specific copy in the multiarch
    # include dir, which the imported target does not add.
    target_include_directories(sage PRIVATE "${SDL2_INCLUDE_DIRS}"
        "${SDL2_PREFIX}/include/${CMAKE_LIBRARY_ARCHITECTURE}")
    target_compile_definitions(sage PRIVATE SAGE_HAS_SDL=1)
    message(STATUS "SDL2 found: building with display output")
else()
    message(STATUS "SDL2 not found: building headless (no window)")
endif()

if(MSVC)
    target_compile_options(sage PRIVATE /W4)
else()
    target_compile_options(sage PRIVATE -Wall -Wextra)
endif()

# Optional convenience target: download the MAME references and the target ROM.
# Not part of the default build (the ROM is large and network-dependent).
add_custom_target(fetch-assets
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_assets.sh"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Fetching MAME references and doa.zip"
    USES_TERMINAL)
