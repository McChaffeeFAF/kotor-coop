# Copyright (c) 2026 The reone project contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

set(TEXGEN_INCLUDE_DIR ${REONE_INCLUDE_DIR}/reone/texgen)
set(TEXGEN_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/libs/texgen)

set(TEXGEN_HEADERS
    ${TEXGEN_INCLUDE_DIR}/generate.h
    ${TEXGEN_INCLUDE_DIR}/image.h
    ${TEXGEN_INCLUDE_DIR}/job.h
    ${TEXGEN_INCLUDE_DIR}/manifest.h
    ${TEXGEN_INCLUDE_DIR}/rules.h
    ${TEXGEN_INCLUDE_DIR}/sources.h)

set(TEXGEN_SOURCES
    ${TEXGEN_SOURCE_DIR}/generate.cpp
    ${TEXGEN_SOURCE_DIR}/image.cpp
    ${TEXGEN_SOURCE_DIR}/job.cpp
    ${TEXGEN_SOURCE_DIR}/manifest.cpp
    ${TEXGEN_SOURCE_DIR}/rules.cpp
    ${TEXGEN_SOURCE_DIR}/sources.cpp)

# The enhanced-texture generator: reads a player's own textures and writes derived maps
# to a local folder. Never part of the player package unless asked for (docs/texgen.md).
add_library(texgen STATIC ${TEXGEN_HEADERS} ${TEXGEN_SOURCES} ${CLANG_FORMAT_PATH})
set_target_properties(texgen PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/lib)
set_target_properties(texgen PROPERTIES DEBUG_POSTFIX "d")
target_precompile_headers(texgen PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(texgen PUBLIC resource graphics system ${Boost_JSON_LIBRARY})
