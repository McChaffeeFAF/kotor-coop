# Copyright (c) 2026 The reone project contributors

# Co-op networking: the wire protocol, install fingerprints and transports.
# The protocol and loopback transport always build; the socket transport
# needs GameNetworkingSockets (REONE_NETWORK, see the root CMakeLists).

set(NET_INCLUDE_DIR ${REONE_INCLUDE_DIR}/reone/net)
set(NET_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/libs/net)

set(NET_HEADERS
    ${NET_INCLUDE_DIR}/bytes.h
    ${NET_INCLUDE_DIR}/fingerprint.h
    ${NET_INCLUDE_DIR}/joincode.h
    ${NET_INCLUDE_DIR}/protocol.h
    ${NET_INCLUDE_DIR}/signallink.h
    ${NET_INCLUDE_DIR}/transport.h)

set(NET_SOURCES
    ${NET_SOURCE_DIR}/bytes.cpp
    ${NET_SOURCE_DIR}/fingerprint.cpp
    ${NET_SOURCE_DIR}/gnstransport.cpp
    ${NET_SOURCE_DIR}/joincode.cpp
    ${NET_SOURCE_DIR}/loopback.cpp
    ${NET_SOURCE_DIR}/protocol.cpp
    ${NET_SOURCE_DIR}/signallink.cpp)

add_library(net STATIC ${NET_HEADERS} ${NET_SOURCES} ${CLANG_FORMAT_PATH})
set_target_properties(net PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/lib)
set_target_properties(net PROPERTIES DEBUG_POSTFIX "d")
target_precompile_headers(net PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(net PRIVATE system ${Boost_JSON_LIBRARY})
if(WIN32)
    target_link_libraries(net PRIVATE ws2_32)
endif()
if(REONE_HAVE_GNS)
    target_compile_definitions(net PRIVATE REONE_HAVE_GNS=1)
    target_link_libraries(net PUBLIC GameNetworkingSockets::GameNetworkingSockets)
endif()
