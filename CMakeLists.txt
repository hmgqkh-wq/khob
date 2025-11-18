cmake_minimum_required(VERSION 3.6)
project(vulkan_xeno_wrapper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

# Build shared library named exactly libvulkan.xeno_wrapper.so
add_library(vulkan.xeno_wrapper SHARED ${SRC_FILES})

set_target_properties(vulkan.xeno.wrapper PROPERTIES PREFIX "lib" OUTPUT_NAME "vulkan.xeno_wrapper")
# Ensure filename is libvulkan.xeno_wrapper.so
set_target_properties(vulkan.xeno.wrapper PROPERTIES OUTPUT_NAME "vulkan.xeno_wrapper")

# Link dl (for dlsym) and pthread
target_link_libraries(vulkan.xeno.wrapper PRIVATE -ldl -lpthread)
