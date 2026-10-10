# Block-compression encoders and the BC7 decoder of Richard Geldreich's bc7enc_rdo
# (https://github.com/richgel999/bc7enc_rdo, MIT or public domain; see LICENSE), vendored
# unmodified. Used by texgen to write BC1/BC5/BC7 DDS files and by the engine to decode BC7
# on GPUs without native support.
add_library(bcenc STATIC bc7decomp.cpp bc7enc.cpp rgbcx.cpp)
target_compile_definitions(bcenc PRIVATE RGBCX_USE_SMALLER_TABLES=1)
target_include_directories(bcenc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(bcenc PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(bcenc PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/lib)
if(MSVC)
    target_compile_options(bcenc PRIVATE /w)
else()
    target_compile_options(bcenc PRIVATE -w)
endif()
