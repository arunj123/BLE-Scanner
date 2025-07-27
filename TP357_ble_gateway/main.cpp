#include <iostream>
#include <thread>   // For std::thread
#include <chrono>   // For std::chrono::seconds
#include <csignal>  // For std::signal
#include <map>      // For std::map to track connected iTags
#include <mutex>    // For std::mutex to protect shared resources

#include "BluetoothScanner.h" // Include the BluetoothScanner class

// Global pointer to the BluetoothScanner instance, for signal handler access
BluetoothScanner* g_scanner_ptr = nullptr;

// Signal handler function to gracefully terminate the program
void signal_handler(int signum) {
    std::cout << "\nSIGINT received. Initiating graceful shutdown..." << std::endl;
    if (g_scanner_ptr) {
        g_scanner_ptr->stopScan(); // Tell the scanner thread to stop
    }
    // No explicit stop for GATT manager here, its destructor will handle cleanup
}

int main(int argc, char **argv) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);

    BluetoothScanner scanner;
    g_scanner_ptr = &scanner; // Assign global scanner pointer

    // Register device handlers
    scanner.registerHandler(std::make_unique<TP357Handler>());

    // Initialize the Bluetooth scanner
    if (!scanner.init()) {
        std::cerr << "Failed to initialize Bluetooth scanner. Exiting." << std::endl;
        return 1;
    }

    // Start the scanning loop in a separate thread
    std::thread scanner_thread(&BluetoothScanner::startScan, &scanner);

    // Main thread can do other work or simply wait for the scanner thread to finish
    // For this example, we'll just join the thread to wait for it to complete
    // (which happens when Ctrl+C is pressed and stopScan() is called).
    scanner_thread.join();

    std::cout << "Main thread exiting." << std::endl;
    return 0;
}
