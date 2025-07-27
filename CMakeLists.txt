cmake_minimum_required(VERSION 3.16)
project(ThermoScanner C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Create a static library for btlib ---
# This allows multiple executables to use the btlib code without compiling it each time.
# This assumes btlib.h and btlib.c are inside a 'btlib' subdirectory.
# If they are in the root, change the path to "btlib.c".
add_library(btlib STATIC btlib/btlib.c)
# Make the btlib header file available to any target that links against the library.
target_include_directories(btlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/btlib)


# --- Original ThermoScanner Application ---
add_executable(thermo_scanner main.cpp BluetoothScanner.cpp)

# Find the Bluetooth library (BlueZ) - may be needed for thermo_scanner
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUETOOTH REQUIRED bluez)

# Link thermo_scanner with its dependencies, including our new btlib library
target_link_libraries(thermo_scanner PRIVATE btlib ${BLUETOOTH_LIBRARIES} pthread)

# Add required capabilities to the final executable to run without sudo
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS thermo_scanner DESTINATION bin)
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:thermo_scanner>)")


# --- Include the iTag application project ---
# This tells CMake to look for another CMakeLists.txt in the 'btlib_app' directory.
add_subdirectory(btlib_app)


# --- Include the CameraDrmDisplay project as a subdirectory ---
# This will build the CameraDrmDisplay executable as part of this project.
add_subdirectory(camera_drm_display)
