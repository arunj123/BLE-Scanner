# TP357_ble_gateway/CMakeLists.txt
# (Assuming this is the CMakeLists.txt inside your TP357_ble_gateway directory)

# Add the tp357_gateway executable
add_executable(tp357_gateway
    main.cpp
    BluetoothScanner.cpp
    MessageQueue.cpp
    SQLiteDatabaseManager.cpp
    DataProcessor.cpp
    EnvReader.cpp
)

# Link tp357_gateway with its dependencies, including btlib, Bluetooth, SQLite3, spdlog, and pthread
target_link_libraries(tp357_gateway PRIVATE
    btlib
    ${BLUETOOTH_LIBRARIES}
    pthread
    ${SQLite3_LIBRARIES}
    sqlite3
    spdlog::spdlog # Link against the spdlog target provided by FetchContent
)

# Add required capabilities to the final executable to run without sudo
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
install(TARGETS tp357_gateway DESTINATION bin)
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:tp357_gateway>)")
