# libFuzzer targets for the file-format readers. Enabled with -DREONE_FUZZ=ON
# (clang only); build one with `cmake --build <dir> --target fuzz_<name>` or use
# tools/cloud/fuzz.sh.

set(FUZZ_FORMATS key bif erf rim gff 2da tlk ssf lip lyt vis mdl tpc tga txi bwm ncs)

foreach(fmt ${FUZZ_FORMATS})
    add_executable(fuzz_${fmt} ${CMAKE_SOURCE_DIR}/fuzz/fuzz_formats.cpp)
    target_compile_definitions(fuzz_${fmt} PRIVATE FUZZ_TARGET_${fmt}=1)
    # The reader headers assume the project-wide precompiled header.
    target_compile_options(fuzz_${fmt} PRIVATE -include ${CMAKE_SOURCE_DIR}/src/pch.h)
    target_link_libraries(fuzz_${fmt} PRIVATE resource graphics script system -fsanitize=fuzzer)
    set_target_properties(fuzz_${fmt} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)
endforeach()

# Writes tiny synthetic seed inputs (never game files) for every target.
add_executable(fuzz_seedgen ${CMAKE_SOURCE_DIR}/fuzz/seedgen.cpp)
set_target_properties(fuzz_seedgen PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)

# The co-op wire format (src/libs/net): every decoder, plus the join-code helpers.
add_executable(fuzz_net ${CMAKE_SOURCE_DIR}/fuzz/fuzz_net.cpp)
target_compile_options(fuzz_net PRIVATE -include ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(fuzz_net PRIVATE net system ${Boost_JSON_LIBRARY} -fsanitize=fuzzer)
set_target_properties(fuzz_net PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)

add_executable(fuzz_netseed ${CMAKE_SOURCE_DIR}/fuzz/netseed.cpp)
target_compile_options(fuzz_netseed PRIVATE -include ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(fuzz_netseed PRIVATE net system ${Boost_JSON_LIBRARY})
set_target_properties(fuzz_netseed PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz)
