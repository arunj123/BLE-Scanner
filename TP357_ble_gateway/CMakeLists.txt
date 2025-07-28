cmake_minimum_required(VERSION 3.16)
project(tp357Gateway C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SQLite3 library
find_package(SQLite3 REQUIRED)

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
# This 'add_subdirectory' command typically handles finding and setting up Firebase targets
add_subdirectory(${FIREBASE_CPP_SDK_DIR} firebase_sdk_build)

# --- Original tp357_gateway Application ---
add_executable(tp357_gateway
    main.cpp
    BluetoothScanner.cpp
    MessageQueue.cpp
    SQLiteDatabaseManager.cpp
    DataProcessor.cpp
    FirestoreManager.cpp
    EnvReader.cpp
)

# IMPORTANT: Add the Firebase SDK's 'include' directory to your target's include paths.
# This tells the compiler where to find headers like 'firebase/app_options.h'.
target_include_directories(tp357_gateway PRIVATE
    ${FIREBASE_CPP_SDK_DIR}/include
)

# Link tp357_gateway with its dependencies, including our new btlib library, SQLite3, pthread, and Firebase
target_link_libraries(tp357_gateway PRIVATE
    btlib
    ${BLUETOOTH_LIBRARIES}
    pthread
    ${SQLite3_LIBRARIES} # Link SQLite3
    firebase_app         # Link Firebase App library
    firebase_firestore   # Link Firebase Firestore library
)

# Add required capabilities to the final executable to run without sudo
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS tp357_gateway DESTINATION bin)
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:tp357_gateway>)")
