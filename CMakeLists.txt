cmake_minimum_required(VERSION 3.10)
project(OrangeHRMClient)

set(CMAKE_C_STANDARD 11)

# Ensure the required packages are installed
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)

# Try to find json-c using pkg-config
pkg_check_modules(JSON_C REQUIRED json-c)

# If json-c is not found, attempt to install it
if (NOT JSON_C_FOUND)
    message(STATUS "json-c not found, attempting to install it")

    execute_process(COMMAND sudo apt-get update)
    execute_process(COMMAND sudo apt-get install -y libjson-c-dev)

    # Try again to find json-c
    pkg_check_modules(JSON_C REQUIRED json-c)
    if (NOT JSON_C_FOUND)
        message(FATAL_ERROR "json-c library could not be installed.")
    endif()
endif()

# Try to find GTK+ 3.0 using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# Include directories for CURL, json-c, and GTK
include_directories(${CURL_INCLUDE_DIRS} ${JSON_C_INCLUDE_DIRS} ${GTK3_INCLUDE_DIRS})

# Add executable
add_executable(orangehrm_client main.c orangehrm_client.c)

# Link libraries (CURL, json-c, pthread and GTK)
target_link_libraries(orangehrm_client ${CURL_LIBRARIES} ${JSON_C_LIBRARIES} ${GTK3_LIBRARIES} pthread)

# Copy config.json to the build folder
file(COPY ${CMAKE_SOURCE_DIR}/config.json DESTINATION ${CMAKE_BINARY_DIR})
