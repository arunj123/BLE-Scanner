# Minimum required CMake version
cmake_minimum_required(VERSION 3.10) # Can be 3.16 if consistent with parent

# Project name for the subdirectory
# This project name will be used internally by CMake for this sub-project.
project(CameraDrmDisplaySub CXX) # Changed project name to avoid potential conflicts with parent if needed

# Set the C++ standard to C++17 (or a version compatible with libcamera)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Diagnostic: Print implicit include directories
message(STATUS "CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES: ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")

# Diagnostic: Check if stdlib.h can be found by CMake's check_include_file
include(CheckIncludeFile)
check_include_file(stdlib.h HAVE_STDLIB_H)
if (NOT HAVE_STDLIB_H)
    message(FATAL_ERROR "stdlib.h not found by CMake's check. This indicates a severe issue with your C/C++ development environment.")
else()
    message(STATUS "stdlib.h found by CMake's check.")
endif()

# Find libcamera
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAMERA REQUIRED libcamera)
if (LIBCAMERA_FOUND)
    message(STATUS "Found libcamera: ${LIBCAMERA_LIBRARIES}")
    include_directories(${LIBCAMERA_INCLUDE_DIRS})
    link_directories(${LIBCAMERA_LIBRARY_DIRS})
    set(LIBCAMERA_LINK_LIBS ${LIBCAMERA_LIBRARIES})
else()
    message(FATAL_ERROR "libcamera not found. Please ensure libcamera-dev is installed and pkg-config works.")
endif()

# Find libdrm and libgbm
pkg_check_modules(LIBDRM REQUIRED libdrm)
if (LIBDRM_FOUND)
    message(STATUS "Found libdrm: ${LIBDRM_LIBRARIES}")
    include_directories(${LIBDRM_INCLUDE_DIRS})
    link_directories(${LIBDRM_LIBRARY_DIRS})
    set(LIBDRM_LINK_LIBS ${LIBDRM_LIBRARIES})
else()
    message(FATAL_ERROR "libdrm not found. Please ensure libdrm-dev is installed and pkg-config works.")
endif()

pkg_check_modules(LIBGBM REQUIRED gbm)
if (LIBGBM_FOUND)
    message(STATUS "Found libgbm: ${LIBGBM_LIBRARIES}")
    include_directories(${LIBGBM_INCLUDE_DIRS})
    link_directories(${LIBGBM_LIBRARY_DIRS})
    set(LIBGBM_LINK_LIBS ${LIBGBM_LIBRARIES})
else()
    message(FATAL_ERROR "libgbm not found. Please ensure libgbm-dev is installed and pkg-config works.")
endif()

# Add the executable target for the camera display
# Changed executable name to avoid conflict with other subdirectories
add_executable(camera_drm_display_app # Renamed executable target
    libcamera-init-framebuffer.cpp
    drm_renderer.cpp
)

# REMOVED: The explicit target_compile_options for /usr/include and /usr/include/aarch64-linux-gnu.
# These paths are typically implicitly handled by the compiler and adding them explicitly
# can interfere with the compiler's internal #include_next mechanism.

# Link the executable with the required libraries
target_link_libraries(camera_drm_display_app # Link the new executable name
    ${LIBCAMERA_LINK_LIBS}

    # Ensure -lrt is linked if needed for real-time extensions (e.g., clock_gettime)
    # Some libcamera versions or system configurations might implicitly require it.
    # If you get undefined reference errors related to clock_gettime or similar, uncomment this.
    # -lrt

    ${LIBDRM_LINK_LIBS}
    ${LIBGBM_LINK_LIBS}
)

# Optional: Set output directory for the executable relative to the *sub-project's* binary dir
# This means it will be in build/camera_drm_display/bin/camera_drm_display_app
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
