# Simulation benchmarks: a plain timed harness, off by default.
#   cmake -DREONE_BENCH=ON ... && cmake --build <dir> --target bench_sim
# or tools/cloud/bench.sh. Uses the unit-test fixtures (mocked services) and
# synthetic scenes only.

if(MSVC)
    find_package(GTest CONFIG REQUIRED)
else()
    find_package(GTest REQUIRED)
endif()

add_executable(bench_sim
    ${CMAKE_SOURCE_DIR}/bench/bench_sim.cpp
    ${CMAKE_SOURCE_DIR}/test/fixtures/engine.cpp)
target_include_directories(bench_sim PRIVATE ${GTEST_INCLUDE_DIRS})
target_precompile_headers(bench_sim PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(bench_sim PRIVATE tools json ${Boost_JSON_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} GTest::gmock)
set_target_properties(bench_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench)
