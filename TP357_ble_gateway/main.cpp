#include <iostream>
#include <thread>   // For std::thread
#include <chrono>   // For std::chrono::seconds
#include <csignal>  // For std::signal
#include <map>      // For std::map to track connected iTags
#include <mutex>    // For std::mutex to protect shared resources
#include <memory>   // For std::unique_ptr

// Include full definitions of classes used as concrete objects or template arguments first
#include "MessageQueue.h"          // Provides full definition of MessageQueue
#include "SQLiteDatabaseManager.h" // Provides full definition of SQLiteDatabaseManager
#include "DataProcessor.h"         // Provides full definition of DataProcessor
#include "EnvReader.h"             // Provides full definition of EnvReader

#include "BluetoothScanner.h"      // Include the BluetoothScanner class

// Global pointers for graceful shutdown
BluetoothScanner* g_scanner_ptr = nullptr;
DataProcessor* g_data_processor_ptr = nullptr;

// Signal handler function to gracefully terminate the program
void signal_handler(int signum) {
    std::cout << "\nSIGINT received. Initiating graceful shutdown..." << std::endl;
    // Stop data processor first, as it relies on the queue
    if (g_data_processor_ptr) {
        g_data_processor_ptr->stopProcessing();
    }
    // Then tell the scanner thread to stop
    if (g_scanner_ptr) {
        g_scanner_ptr->stopScan();
    }
}

int main(int argc, char **argv) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);

    // --- Read settings from .env file ---
    EnvReader env_reader;
    if (!env_reader.load(".env")) {
        std::cerr << "Could not load .env file. Using default settings." << std::endl;
    }

    // Get logging window duration from .env or use a default (e.g., 5 seconds)
    int logging_window_seconds = std::stoi(env_reader.getOrDefault("LOGGING_WINDOW_SECONDS", "2")); // Keep it short for debugging
    std::cout << "Configured logging window: " << logging_window_seconds << " seconds." << std::endl;

    // Create the message queue
    MessageQueue sensor_data_queue;

    // Create the Bluetooth scanner instance
    BluetoothScanner scanner;
    g_scanner_ptr = &scanner; // Assign global scanner pointer

    // Create a TP357Handler instance
    auto tp357_handler = std::make_unique<TP357Handler>();

    // Set the message queue for the handler so it can push data
    tp357_handler->setMessageQueue(&sensor_data_queue);

    // Register predefined names for your TP357 devices
    tp357_handler->setDeviceName("E2:76:F5:4B:E4:F0", "Living Room Sensor");
    tp357_handler->setDeviceName("F8:5F:2B:62:E5:F5", "Kitchen Sensor");
    tp357_handler->setDeviceName("DF:50:8B:21:84:89", "Bedroom Sensor");
    tp357_handler->setDeviceName("D6:05:85:FD:C0:BC", "Outdoor Sensor");
    tp357_handler->setDeviceName("CE:2C:40:3C:73:F7", "Garage Sensor");
    tp357_handler->setDeviceName("E9:D5:D2:C9:B8:7C", "Hallway Sensor");

    // Register the handler with the scanner
    scanner.registerHandler(std::move(tp357_handler));

    // Initialize the Bluetooth scanner
    if (!scanner.init()) {
        std::cerr << "Failed to initialize Bluetooth scanner. Exiting." << std::endl;
        return 1;
    }

    // Create and initialize the SQLite database manager
    auto sqlite_db_manager = std::make_unique<SQLiteDatabaseManager>();
    if (!sqlite_db_manager->initialize("sensor_readings.db")) {
        std::cerr << "Failed to initialize SQLite database. Exiting." << std::endl;
        scanner.stopScan(); // Ensure scanner is stopped if DB init fails
        return 1;
    }

    // --- Start DataProcessor BEFORE Scanner ---
    std::cout << "Attempting to start DataProcessor..." << std::endl;
    DataProcessor data_processor(sensor_data_queue, std::move(sqlite_db_manager), logging_window_seconds);
    g_data_processor_ptr = &data_processor; // Assign global data processor pointer
    data_processor.startProcessing();
    std::cout << "DataProcessor start attempt complete." << std::endl;
    // --- End DataProcessor Start ---


    // Start the scanning loop in a separate thread
    std::cout << "Attempting to start BluetoothScanner..." << std::endl;
    std::thread scanner_thread(&BluetoothScanner::startScan, &scanner);
    std::cout << "BluetoothScanner start attempt complete." << std::endl;


    // Main thread waits for the scanner thread to finish
    // (which happens when Ctrl+C is pressed and stopScan() is called).
    scanner_thread.join();

    // Ensure data processor also finishes after scanner, if not already stopped by signal handler
    data_processor.stopProcessing();

    std::cout << "Main thread exiting." << std::endl;
    return 0;
}
