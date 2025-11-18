cmake_minimum_required(VERSION 3.10)
project(vulkan_xeno_wrapper LANGUAGES C)

# Android + Vulkan
find_library(log-lib log)
find_library(android-lib android)
find_library(vulkan-lib vulkan)

# Build the B-Easy wrapper .so
add_library(vulkan.xeno.wrapper SHARED
    xeno_wrapper.c
)

# Include directory for the wrapper
target_include_directories(vulkan.xeno.wrapper PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Link system dependencies
target_link_libraries(vulkan.xeno.wrapper
    ${vulkan-lib}
    ${log-lib}
    ${android-lib}
)

# Make sure output name matches Eden expectation
set_target_properties(vulkan.xeno.wrapper PROPERTIES
    OUTPUT_NAME "libvulkan.xeno_wrapper"
)
