cmake_minimum_required(VERSION 3.16)
project(tp357Gateway C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SQLite3 library
find_package(SQLite3 REQUIRED)

# --- Original tp357_gateway Application ---
add_executable(tp357_gateway
    main.cpp
    BluetoothScanner.cpp
    MessageQueue.cpp
    SQLiteDatabaseManager.cpp
    DataProcessor.cpp
    FirestoreManager.cpp # Add the new FirestoreManager source file
)

# Link tp357_gateway with its dependencies, including our new btlib library, SQLite3, and pthread
target_link_libraries(tp357_gateway PRIVATE
    btlib
    ${BLUETOOTH_LIBRARIES}
    pthread
    ${SQLite3_LIBRARIES} # Link SQLite3
    # If using real Firebase Admin SDK, you would link it here:
    # firebase_app
    # firebase_firestore
)

# Add required capabilities to the final executable to run without sudo
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS tp357_gateway DESTINATION bin)
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:tp357_gateway>)")
