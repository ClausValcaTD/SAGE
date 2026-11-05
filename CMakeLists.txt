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

add_executable(sage
    src/main.cpp
    src/rom_loader.cpp)

target_include_directories(sage PRIVATE include)
target_link_libraries(sage PRIVATE miniz)

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
