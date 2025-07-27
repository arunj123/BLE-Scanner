cmake_minimum_required(VERSION 3.16)
project(tp357Gateway C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Original tp357_gateway Application ---
add_executable(tp357_gateway main.cpp BluetoothScanner.cpp)

# Link tp357_gateway with its dependencies, including our new btlib library
target_link_libraries(tp357_gateway PRIVATE btlib ${BLUETOOTH_LIBRARIES} pthread)

# Add required capabilities to the final executable to run without sudo
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS tp357_gateway DESTINATION bin)
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:tp357_gateway>)")
