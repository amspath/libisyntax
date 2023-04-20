cmake_minimum_required(VERSION 3.15)
project(libisyntax)

set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    message(STATUS "Enabling ARM NEON support")
    add_compile_options(-mfpu=neon)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/src/platform")
include_directories("${CMAKE_SOURCE_DIR}/src/utils")
include_directories("${CMAKE_SOURCE_DIR}/src/isyntax")
include_directories("${CMAKE_SOURCE_DIR}/src/third_party")

set(LIBISYNTAX_COMMON_SOURCE_FILES
        src/libisyntax.c
        src/isyntax/isyntax.c
        src/isyntax/isyntax_reader.c
        src/utils/timerutils.c
        src/utils/block_allocator.c
        src/utils/benaphore.c
        src/platform/platform.c
        src/platform/work_queue.c
        src/third_party/yxml.c
        src/third_party/ltalloc.cc
)

if (WIN32)
    set(LIBISYNTAX_COMMON_SOURCE_FILES ${LIBISYNTAX_COMMON_SOURCE_FILES} src/platform/win32_utils.c)
else()
    set(LIBISYNTAX_COMMON_SOURCE_FILES ${LIBISYNTAX_COMMON_SOURCE_FILES} src/platform/linux_utils.c)
endif()

add_library(libisyntax
        ${LIBISYNTAX_COMMON_SOURCE_FILES}
)

add_executable(isyntax_example
        src/isyntax_example.c
        ${LIBISYNTAX_COMMON_SOURCE_FILES}
)

if (WIN32)
    target_link_libraries(libisyntax winmm)
    target_link_libraries(isyntax_example winmm)
else()

endif()

# TODO(avirodov): Consider moving testing to its own test/CMakeLists. This will require moving the library building
#   to src/CMakeLists to avoid circular deps.

# Note: checking if running in test mode to avoid downloading a large test file for regular builds.
# TODO(avirodov): doesn't actually work. For now downloading test files even if just want to build.
#option(BUILD_TESTING "" OFF)
include(CTest)
# message(BUILD_TESTING="${BUILD_TESTING}")
# if(BUILD_TESTING)

add_test(NAME smoke_example_runs_no_args
        COMMAND isyntax_example)

# Thank you https://gitlab.com/BioimageInformaticsGroup/openphi
if (NOT EXISTS ${CMAKE_BINARY_DIR}/testslide.isyntax)
    file(DOWNLOAD https://zenodo.org/record/5037046/files/testslide.isyntax?download=1 ${CMAKE_BINARY_DIR}/testslide.isyntax SHOW_PROGRESS)
endif()

# Test that we can show levels and that number of tiles shown is as expected for this test tile.
add_test(NAME smoke_example_runs_with_test_slide_showing_levels
        COMMAND isyntax_example ${CMAKE_BINARY_DIR}/testslide.isyntax)
set_tests_properties(smoke_example_runs_with_test_slide_showing_levels
        PROPERTIES PASS_REGULAR_EXPRESSION "width.*=256")
set_tests_properties(smoke_example_runs_with_test_slide_showing_levels
        PROPERTIES PASS_REGULAR_EXPRESSION "height.*=384")

# Test that we can produce a tile png.
add_test(NAME smoke_example_runs_with_test_slide_producing_output
        COMMAND isyntax_example ${CMAKE_BINARY_DIR}/testslide.isyntax 3 5 10 ${CMAKE_BINARY_DIR}/output_smoke_example_runs_with_test_slide_producing_output.png)

# Test that the tile png was indeed produced.
add_test(NAME smoke_example_runs_with_test_slide_produced_output
        COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_BINARY_DIR}/output_smoke_example_runs_with_test_slide_producing_output.png)
set_tests_properties(smoke_example_runs_with_test_slide_produced_output PROPERTIES DEPENDS smoke_example_runs_with_test_slide_producing_output)

# Regression test that the produced tile pixels did not change from expected.
add_test(NAME regression_example_tile_3_5_10_pixel_check
        COMMAND ${CMAKE_COMMAND} -E compare_files ../test/expected_output/testslide_example_tile_3_5_10.png ${CMAKE_BINARY_DIR}/output_smoke_example_runs_with_test_slide_producing_output.png)
set_tests_properties(regression_example_tile_3_5_10_pixel_check PROPERTIES DEPENDS smoke_example_runs_with_test_slide_producing_output)

# endif()  # if(BUILD_TESTING)


