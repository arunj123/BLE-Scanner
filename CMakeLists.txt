cmake_minimum_required(VERSION 3.16)
project(Home C CXX)

# --- Create a static library for btlib ---
# This allows multiple executables to use the btlib code without compiling it each time.
# This assumes btlib.h and btlib.c are inside a 'btlib' subdirectory.
# If they are in the root, change the path to "btlib.c".
add_library(btlib STATIC btlib/btlib.c)
# Make the btlib header file available to any target that links against the library.
target_include_directories(btlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/btlib)

# Find the Bluetooth library (BlueZ) - may be needed for tp357_gateway
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUETOOTH REQUIRED bluez)

# --- Firebase Admin SDK Configuration ---
# You MUST set the FIREBASE_CPP_SDK_DIR environment variable
# or provide it as a CMake argument (e.g., -DFIREBASE_CPP_SDK_DIR=/path/to/firebase_cpp_sdk)
# Point this to the root directory of your unpacked Firebase C++ SDK.
if(NOT DEFINED ENV{FIREBASE_CPP_SDK_DIR} AND NOT FIREBASE_CPP_SDK_DIR)
    message(FATAL_ERROR "FIREBASE_CPP_SDK_DIR environment variable or CMake argument is not set.
    Please set it to the root directory of your Firebase C++ SDK installation.")
endif()

# Use the environment variable if set, otherwise use the CMake argument
if(DEFINED ENV{FIREBASE_CPP_SDK_DIR})
    set(FIREBASE_CPP_SDK_DIR "$ENV{FIREBASE_CPP_SDK_DIR}")
endif()

# Add Firebase Admin SDK to your project
# This 'add_subdirectory' command processes the SDK's own CMakeLists.txt
# and makes Firebase targets (like firebase_app, firebase_firestore) available.
add_subdirectory(${FIREBASE_CPP_SDK_DIR} firebase_sdk_build)

# --- Include the original tp357_gateway application project ---
# This tells CMake to look for another CMakeLists.txt in the 'TP357_ble_gateway' directory.
add_subdirectory(TP357_ble_gateway)

# --- Include the iTag application project ---
# This tells CMake to look for another CMakeLists.txt in the 'btlib_app' directory.
add_subdirectory(btlib_app)

# --- Include the CameraDrmDisplay project as a subdirectory ---
# This will build the CameraDrmDisplay executable as part of this project.
add_subdirectory(camera_drm_display)
