cmake_minimum_required(VERSION 3.15)
project(libisyntax)

set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64.*|AARCH64.*|arm64.*|ARM64.*)")
    set(IS_ARM64 TRUE)
    set(ARM_ARCH "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm.*|ARM.*)")
    set(IS_ARM TRUE)
    set(ARM_ARCH "arm")
endif()

if(APPLE AND (IS_ARM OR IS_ARM64))
    set(IS_APPLE_SILICON TRUE)
endif()

if(IS_APPLE_SILICON)
    message(STATUS "Detected Apple silicon ${ARM_ARCH} architecture")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch ${ARM_ARCH}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch ${ARM_ARCH}")
    # Enable NEON for ARM-based Apple Silicon
    add_compile_options(-march=armv8.2-a+fp16+simd)
endif()


option(ENABLE_MEMORY_CHECK "Enable memory check with sanitizers (requires DEBUG mode)" OFF)

if(ENABLE_MEMORY_CHECK)
    if(NOT CMAKE_BUILD_TYPE MATCHES Debug)
        message(FATAL_ERROR "ENABLE_MEMORY_CHECK is only allowed in Debug build mode")
    endif()

    if(APPLE)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -O1 -fno-omit-frame-pointer -g")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
    elseif(UNIX AND NOT APPLE) # Linux
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g")
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og -g")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og -g")
    endif()
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

if (WIN32)
    target_link_libraries(libisyntax winmm)
else()
endif()

add_executable(isyntax_example src/isyntax_example.c)
target_link_libraries(isyntax_example libisyntax)

# Find LibTIFF library
find_package(TIFF REQUIRED)
if (NOT TIFF_FOUND)
    message(WARNING "LibTIFF not found")
    message(WARNING "Will not compile `isyntax-to-tiff` utility")
else()
    include_directories(${TIFF_INCLUDE_DIR})
    add_executable(isyntax-to-tiff
            src/isyntax_to_tiff.c
            ${LIBISYNTAX_COMMON_SOURCE_FILES} ${TIFF_LIBRARIES})
target_link_libraries(isyntax-to-tiff PRIVATE ${TIFF_LIBRARIES})
endif()

# TODO(avirodov): Consider moving testing to its own test/CMakeLists. This will require moving the library building
#   to src/CMakeLists to avoid circular deps.

# Note: checking if running in test mode to avoid downloading a large test file for regular builds.
option(BUILD_TESTING "Build tests" OFF)
message(STATUS "BUILD_TESTING = ${BUILD_TESTING}")
if(BUILD_TESTING)
    include(CTest)
    add_test(NAME smoke_example_runs_no_args
            COMMAND isyntax_example)

    # Thank you https://gitlab.com/BioimageInformaticsGroup/openphi
    if (NOT EXISTS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/testslide.isyntax)
        file(DOWNLOAD https://zenodo.org/record/5037046/files/testslide.isyntax?download=1 ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/testslide.isyntax SHOW_PROGRESS)
    else()
        message(STATUS "Found test slide (no need to download): ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/testslide.isyntax")
    endif()

    # Test that we can show levels and that number of tiles shown is as expected for this test tile.
    add_test(NAME smoke_example_runs_with_test_slide_showing_levels
            COMMAND isyntax_example ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/testslide.isyntax)
    set_tests_properties(smoke_example_runs_with_test_slide_showing_levels
            PROPERTIES PASS_REGULAR_EXPRESSION "width.*=256")
    set_tests_properties(smoke_example_runs_with_test_slide_showing_levels
            PROPERTIES PASS_REGULAR_EXPRESSION "height.*=384")

    # Test that we can produce a tile png.
    add_test(NAME smoke_example_runs_with_test_slide_producing_output
            COMMAND isyntax_example ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/testslide.isyntax 3 5 10 ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/output_smoke_example_runs_with_test_slide_producing_output.png)

    # Test that the tile png was indeed produced.
    add_test(NAME smoke_example_runs_with_test_slide_produced_output
            COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/output_smoke_example_runs_with_test_slide_producing_output.png)
    set_tests_properties(smoke_example_runs_with_test_slide_produced_output PROPERTIES DEPENDS smoke_example_runs_with_test_slide_producing_output)

    # Regression test that the produced tile pixels did not change from expected.
    add_test(NAME regression_example_tile_3_5_10_pixel_check
            COMMAND ${CMAKE_COMMAND} -E compare_files ../test/expected_output/testslide_example_tile_3_5_10.png ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/output_smoke_example_runs_with_test_slide_producing_output.png)
    set_tests_properties(regression_example_tile_3_5_10_pixel_check PROPERTIES DEPENDS smoke_example_runs_with_test_slide_producing_output)

    add_executable(thread_test test/thread_test.c)
    target_link_libraries(thread_test libisyntax)
    add_test(NAME smoke_thread_test
            COMMAND thread_test)

endif() # if(BUILD_TESTING)


