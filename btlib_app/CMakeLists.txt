# --- iTag Application Executable ---

# Define the executable from itag.cpp
add_executable(itag_app itag.cpp)

# Link the executable against the btlib library (defined in the parent CMakeLists.txt)
# and pthread for std::thread.
target_link_libraries(itag_app PRIVATE btlib pthread)

# --- Installation and Permissions ---

# Install the executable to the 'bin' directory upon 'make install'
install(TARGETS itag_app DESTINATION bin)

# After building, run a command to set the necessary network capabilities
# on the executable, allowing it to run without sudo.
install(CODE "execute_process(COMMAND sudo setcap 'cap_net_raw,cap_net_admin+eip' $<TARGET_FILE:itag_app>)")
