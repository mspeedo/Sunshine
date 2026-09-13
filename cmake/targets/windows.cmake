# windows specific target definitions
set_target_properties(sunshine PROPERTIES LINK_SEARCH_START_STATIC 1)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
find_library(ZLIB ZLIB1)
list(APPEND SUNSHINE_EXTERNAL_LIBRARIES
        $<TARGET_OBJECTS:sunshine_rc_object>
        Advapi32.lib
        Windowsapp.lib
        Wtsapi32.lib
        version.lib)

# Private Moonlight/Sunshine latency benchmark companion. The helper remains a
# separate D3D11 desktop process so desktop capture latency stays in the metric.
add_subdirectory(
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-latency-helper"
        "${CMAKE_BINARY_DIR}/third-party/moonlight-latency-helper")
set(LATENCY_BENCHMARK_SOURCES
        "${CMAKE_SOURCE_DIR}/src/latency_benchmark.cpp"
        "${CMAKE_SOURCE_DIR}/src/latency_benchmark.h"
        "${CMAKE_SOURCE_DIR}/src/latency_benchmark_http.cpp"
        "${CMAKE_SOURCE_DIR}/src/latency_benchmark_http.h")
target_sources(sunshine PRIVATE ${LATENCY_BENCHMARK_SOURCES})
# tests/CMakeLists.txt builds from SUNSHINE_TARGET_FILES, so keep these sources
# in that shared list too. Otherwise nvhttp.cpp/stream.cpp reference benchmark
# functions that the test executable never links.
list(APPEND SUNSHINE_TARGET_FILES ${LATENCY_BENCHMARK_SOURCES})

# Building the Sunshine target directly must also produce a runnable benchmark
# setup. Multi-config generators otherwise leave the helper in its sub-build
# directory while latency_benchmark.cpp intentionally resolves it next to
# sunshine.exe. Packaging still installs the same target normally.
add_dependencies(sunshine moonlight-latency-helper)
add_custom_command(TARGET sunshine POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:moonlight-latency-helper>"
                "$<TARGET_FILE_DIR:sunshine>"
        VERBATIM)
