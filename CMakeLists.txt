cmake_minimum_required(VERSION 3.7.0 FATAL_ERROR)

# Ref:
# https://gitlab.com/christophacham/cmake-kernel-module
# https://gitioc.upc.edu/open-source/xenomai-cmake/blob/master/cmake-modules/FindKernelHeaders.cmake
# https://stackoverflow.com/questions/50877135/cmake-specify-linux-kernel-module-output-build-directory

project(networkfs LANGUAGES C)

# List driver sources
set(SOURCES entrypoint.c http.c)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-D__KERNEL__ -DMODULE)

if(NOT DEFINED ARCH)
        set(ARCH x86)
endif(NOT DEFINED ARCH)

if(NOT DEFINED KERNELHEADERS_DIR)
        # Find the kernel release
        execute_process(
                COMMAND uname -r
                OUTPUT_VARIABLE KERNEL_RELEASE
                OUTPUT_STRIP_TRAILING_WHITESPACE)

        # Find the headers
        find_path(
                KERNELHEADERS_DIR
                include/linux/user.h
                PATHS /usr/src/linux-headers-${KERNEL_RELEASE})
endif(NOT DEFINED KERNELHEADERS_DIR)

message(STATUS "Kernel release: ${KERNEL_RELEASE}")
message(STATUS "Kernel headers: ${KERNELHEADERS_DIR}")

# Add header files
include_directories(
        ${KERNELHEADERS_DIR}/include
        ${KERNELHEADERS_DIR}/include/uapi
        ${KERNELHEADERS_DIR}/include/generated/uapi
        ${KERNELHEADERS_DIR}/arch/${ARCH}/include
        ${KERNELHEADERS_DIR}/arch/${ARCH}/include/generated
        ${KERNELHEADERS_DIR}/arch/${ARCH}/include/uapi
        ${KERNELHEADERS_DIR}/arch/${ARCH}/include/generated/uapi)

set(DRIVER_FILE networkfs.ko)
set(KBUILD_CMD $(MAKE) -C ${KERNELHEADERS_DIR} modules M=${CMAKE_CURRENT_BINARY_DIR} src=${CMAKE_CURRENT_SOURCE_DIR})

string(REPLACE ";" " " SOURCES_LINE "${SOURCES}")

# Generate the Kbuild file through cmake.
file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild
        "obj-m := ${CMAKE_PROJECT_NAME}.o
${CMAKE_PROJECT_NAME}-srcs := ${SOURCES_LINE}
${CMAKE_PROJECT_NAME}-y := $(${CMAKE_PROJECT_NAME}-srcs:.c=.o)
ccflags-y := -std=gnu17 -Wall -Werror -Wno-declaration-after-statement")

add_custom_command(OUTPUT ${DRIVER_FILE}
        COMMAND ${KBUILD_CMD}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${SOURCES} VERBATIM)

add_custom_target(networkfs ALL DEPENDS ${DRIVER_FILE})

add_library(dummy ${SOURCES})
set_target_properties(dummy PROPERTIES EXCLUDE_FROM_ALL 1
                                       EXCLUDE_FROM_DEFAULT_BUILD 1)

enable_testing()

file(CREATE_LINK ${PROJECT_SOURCE_DIR}/tests ${CMAKE_BINARY_DIR}/tests SYMBOLIC)

add_test(test_build
        "${CMAKE_COMMAND}"
        --build "${CMAKE_BINARY_DIR}"
        --target networkfs
)
set_tests_properties(test_build PROPERTIES FIXTURES_SETUP test_setup)
add_test(tests python3 -m tests BasicTestCases -f)
add_test(bonus-name python3 -m tests NameTestCases -f)
add_test(bonus-wr python3 -m tests WRTestCases -f)
add_test(bonus-link python3 -m tests LinkTestCases -f)
set_tests_properties(tests bonus-name bonus-wr bonus-link PROPERTIES RUN_SERIAL TRUE
                                                                     FIXTURES_REQUIRED test_setup)
