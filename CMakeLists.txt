cmake_minimum_required(VERSION 3.16)
project(ThermoScanner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the new source files
add_executable(thermo_scanner main.cpp BluetoothScanner.cpp)

# Find the Bluetooth library (BlueZ)
find_package(PkgConfig REQUIRED)
pkg_check_modules(BLUETOOTH REQUIRED bluez)

# Link with Bluetooth libraries and pthreads for threading
target_link_libraries(thermo_scanner PRIVATE ${BLUETOOTH_LIBRARIES} pthread)

# Add required capabilities to the final executable to run without sudo
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS thermo_scanner DESTINATION bin)
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:thermo_scanner>)")

