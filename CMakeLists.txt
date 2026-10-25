# Copyright (c) 2020-2023 The reone project contributors

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)
project(reone)

set(CMAKE_CXX_STANDARD 17)
set(CLANG_FORMAT_PATH ${CMAKE_SOURCE_DIR}/.clang-format)

# Options

option(BUILD_TESTS "build tests" ON)
option(BUILD_LAUNCHER "build launcher application" ON)
option(BUILD_TOOLKIT "build toolkit application" ON)
option(BUILD_DATAMINER "build dataminer application" ON)
option(BUILD_GFF2JSON "build gff2json application" ON)
option(BUILD_UNERF "build unerf application" ON)
option(BUILD_NCSRUN "build ncsrun application" ON)
option(BUILD_TEXGEN "build the texgen enhanced-texture generator (never part of the player package)" ON)

option(ENABLE_MOVIE "enable movie playback" ON)
option(REONE_NETWORK "co-op networking through GameNetworkingSockets (off at runtime unless --host/--join)" ON)
option(ENABLE_ASAN "enable address sanitizer" OFF)
option(REONE_ASAN "build with AddressSanitizer (GCC/Clang)" OFF)
option(REONE_UBSAN "build with UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(REONE_TSAN "build with ThreadSanitizer (GCC/Clang; exclusive with REONE_ASAN)" OFF)
option(REONE_BENCH "build the simulation benchmarks (bench/, opt-in)" OFF)
option(REONE_FUZZ "build libFuzzer targets for the file-format readers (Clang only; see fuzz/)" OFF)

# END Options

# Dependencies

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
find_package(Boost REQUIRED COMPONENTS program_options exception json)
find_package(OpenGL REQUIRED)
find_package(MAD REQUIRED)

if(MSVC)
    find_package(OpenAL CONFIG REQUIRED)
    find_package(SDL3 CONFIG REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OpenAL REQUIRED openal)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    find_package(Threads REQUIRED)
endif()

if(ENABLE_MOVIE)
    if(MSVC)
        find_package(FFMPEG REQUIRED)
    else()
        pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libavutil libswresample libswscale)
    endif()
    include_directories(${FFMPEG_INCLUDE_DIRS})
endif()

if(BUILD_LAUNCHER OR BUILD_TOOLKIT)
    find_package(wxWidgets REQUIRED core base adv stc gl aui)
    include(${wxWidgets_USE_FILE})
endif()

# Co-op transport. Optional: without the package the engine builds with the
# protocol and session code but no socket transport (--host/--join refuse).
set(REONE_HAVE_GNS OFF)
if(REONE_NETWORK)
    if(NOT MSVC)
        # A GameNetworkingSockets built from source names protobuf and OpenSSL by
        # imported target in its config without finding them; find them first.
        find_package(OpenSSL QUIET)
        find_package(Protobuf QUIET)
    endif()
    find_package(GameNetworkingSockets CONFIG QUIET)
    if(GameNetworkingSockets_FOUND OR TARGET GameNetworkingSockets::GameNetworkingSockets)
        set(REONE_HAVE_GNS ON)
        message(STATUS "Co-op networking: GameNetworkingSockets found")
    else()
        message(WARNING "REONE_NETWORK is ON but GameNetworkingSockets was not found; building without a network transport")
    endif()
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/extern/dearimgui)

set(REONE_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
set(GLAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/extern/glad/include)
include_directories(${Boost_INCLUDE_DIRS} ${SDL3_INCLUDE_DIRS} ${MAD_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/extern/glm ${REONE_INCLUDE_DIR} ${GLAD_INCLUDE_DIR} ${IMGUI_DIR})

add_compile_definitions(BOOST_BIND_GLOBAL_PLACEHOLDERS)

# GLM GTX extensions (euler_angles et al.) are gated behind this define in
# recent GLM releases; required when a system/vcpkg GLM shadows extern/glm.
add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# END Dependencies

# Compile options

# GCC fuses a * b + c into one rounding on FMA-capable targets by default,
# MSVC does not; the simulation must round the same way everywhere.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

if(ENABLE_ASAN)
    if (MSVC)
        add_compile_options("/fsanitize=address")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

if(REONE_ASAN OR REONE_UBSAN OR REONE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "REONE_ASAN/UBSAN/TSAN need GCC or Clang")
    endif()
    if(REONE_TSAN AND REONE_ASAN)
        message(FATAL_ERROR "REONE_TSAN cannot be combined with REONE_ASAN")
    endif()
    set(REONE_SANITIZERS "")
    if(REONE_ASAN)
        list(APPEND REONE_SANITIZERS address)
    endif()
    if(REONE_UBSAN)
        list(APPEND REONE_SANITIZERS undefined)
    endif()
    if(REONE_TSAN)
        list(APPEND REONE_SANITIZERS thread)
    endif()
    list(JOIN REONE_SANITIZERS "," REONE_SANITIZE_ARG)
    add_compile_options(-fsanitize=${REONE_SANITIZE_ARG} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${REONE_SANITIZE_ARG})
    message(STATUS "Sanitizers: ${REONE_SANITIZE_ARG}")
endif()

if(REONE_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "REONE_FUZZ needs Clang (libFuzzer)")
    endif()
    # Instrument everything for coverage; undefined behaviour is a finding, so
    # make it abort instead of just printing.
    add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g1)
    add_link_options(-fsanitize=address,undefined)
endif()

# END Compile options

# Libraries

add_subdirectory(extern/bcenc) # BC1/BC5/BC7 encoders and decoder (vendored bc7enc_rdo)
add_subdirectory(src/libs/system) # system static library
add_subdirectory(src/libs/resource) # resource static library
add_subdirectory(src/libs/graphics) # graphics static library
add_subdirectory(src/libs/audio) # audio static library
add_subdirectory(src/libs/input) # input static library
add_subdirectory(src/libs/json) # json static library
add_subdirectory(src/libs/gui) # gui static library
add_subdirectory(src/libs/scene) # scene static library
add_subdirectory(src/libs/movie) # movie static library
add_subdirectory(src/libs/script) # script static library
add_subdirectory(src/libs/net) # net static library (co-op protocol and transport)
add_subdirectory(src/libs/game) # game static library
add_subdirectory(src/libs/tools) # tools static library
add_subdirectory(src/libs/texgen) # texgen static library (enhanced-texture generator)

# END Libraries

# Applications

add_subdirectory(src/apps/shaderpack) # shaderpack application
add_subdirectory(src/apps/engine) # engine application

if(BUILD_LAUNCHER)
    add_subdirectory(src/apps/launcher) # launcher application
endif()

if(BUILD_TOOLKIT)
    add_subdirectory(src/apps/toolkit) # toolkit application
endif()

if(BUILD_DATAMINER)
    add_subdirectory(src/apps/dataminer) # dataminer application
endif()

if(BUILD_GFF2JSON)
    add_subdirectory(src/apps/gff2json) # gff2json application
endif()

if(BUILD_UNERF)
    add_subdirectory(src/apps/unerf) # unpack ERF files
endif()

if(BUILD_NCSRUN)
    add_subdirectory(src/apps/ncsrun) # execute NCS program
endif()

if(BUILD_TEXGEN)
    add_subdirectory(src/apps/texgen) # texgen command-line tool
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test) # tests executable
endif()

if(REONE_FUZZ)
    add_subdirectory(fuzz) # libFuzzer targets
endif()

if(REONE_BENCH)
    add_subdirectory(bench) # simulation benchmarks
endif()

# END Applications

# Installation

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
endif()

install(TARGETS ${InstallTargets} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS system resource graphics audio scene movie script gui net game tools DESTINATION ${CMAKE_INSTALL_LIBDIR})

# END Installation
