cmake_minimum_required(VERSION 3.23)
project(libisyntax)

set(CMAKE_C_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

include_directories("${CMAKE_SOURCE_DIR}/src")
include_directories("${CMAKE_SOURCE_DIR}/src/platform")
include_directories("${CMAKE_SOURCE_DIR}/src/utils")
include_directories("${CMAKE_SOURCE_DIR}/src/isyntax")
include_directories("${CMAKE_SOURCE_DIR}/src/third_party")
include_directories(${VIPS_INCLUDE_DIRS})

find_package(PkgConfig REQUIRED)
pkg_check_modules(VIPS REQUIRED vips)


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
message(STATUS "VIPS_INCLUDE_DIRS: ${VIPS_INCLUDE_DIRS}")
message(STATUS "VIPS_LIBRARY_DIRS: ${VIPS_LIBRARY_DIRS}")
message(STATUS "VIPS_LIBRARIES: ${VIPS_LIBRARIES}")

link_directories(${VIPS_LIBRARY_DIRS})
add_executable(isyntax-to-tiff
        src/isyntax_to_tiff.c
        ${LIBISYNTAX_COMMON_SOURCE_FILES}
        )

target_include_directories(isyntax-to-tiff PRIVATE ${VIPS_INCLUDE_DIRS})
target_link_libraries(isyntax-to-tiff PRIVATE ${VIPS_LIBRARIES})


if (WIN32)
    target_link_libraries(libisyntax winmm)
    target_link_libraries(isyntax_example winmm)
    target_link_libraries(isyntax-to-tiff winmm)

else()

endif()
