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
#include "FirestoreManager.h"      // Provides full definition of FirestoreManager
#include "DataProcessor.h"         // Provides full definition of DataProcessor
#include "EnvReader.h"             // Provides full definition of EnvReader

#include "BluetoothScanner.h"      // Include the BluetoothScanner class

// Global pointers for graceful shutdown
BluetoothScanner* g_scanner_ptr = nullptr;
DataProcessor* g_data_processor_ptr = nullptr;

// Signal handler function to gracefully terminate the program
void signal_handler(int signum) {
    std::cout << "\nSIGINT received. Initiating graceful shutdown..." << std::endl;
    if (g_data_processor_ptr) {
        g_data_processor_ptr->stopProcessing(); // Stop data processing first
    }
    if (g_scanner_ptr) {
        g_scanner_ptr->stopScan(); // Then tell the scanner thread to stop
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

    // Get Firestore config path from .env or use a default placeholder
    // This path should point to your Firebase service account key JSON file.
    std::string firestore_config_path = env_reader.getOrDefault("FIRESTORE_CONFIG_PATH", "default_firestore_service_account.json");
    std::cout << "Using Firestore config path: " << firestore_config_path << std::endl;

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
    // Based on the provided log output
    tp357_handler->setDeviceName("E2:76:F5:4B:E4:F0", "Living Room Sensor");
    tp357_handler->setDeviceName("F8:5F:2B:62:E5:F5", "Kitchen Sensor");
    tp357_handler->setDeviceName("DF:50:8B:21:84:89", "Bedroom Sensor");
    tp357_handler->setDeviceName("D6:05:85:FD:C0:BC", "Outdoor Sensor");
    tp357_handler->setDeviceName("CE:2C:40:3C:73:F7", "Garage Sensor");

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

    // Create and initialize the Firestore manager
    auto firestore_manager = std::make_unique<FirestoreManager>();
    if (!firestore_manager->initialize(firestore_config_path)) { // Use path from .env
        std::cerr << "Failed to initialize Firestore manager. Proceeding with SQLite only." << std::endl;
        // If Firestore init fails, DataProcessor will effectively only use SQLite.
        // No need to exit here, as SQLite is the fallback.
    }

    // Explicitly set Firestore to be online for real integration attempt
    firestore_manager->setSimulatedOnlineStatus(true);

    // Create the data processor, passing both message queue and database managers
    DataProcessor data_processor(sensor_data_queue, std::move(sqlite_db_manager), std::move(firestore_manager));
    g_data_processor_ptr = &data_processor; // Assign global data processor pointer

    // Start the data processing loop in a separate thread
    data_processor.startProcessing();

    // Start the scanning loop in a separate thread
    std::thread scanner_thread(&BluetoothScanner::startScan, &scanner);

    // Main thread waits for the scanner thread to finish
    // (which happens when Ctrl+C is pressed and stopScan() is called).
    scanner_thread.join();

    // Ensure data processor also finishes after scanner, if not already stopped by signal handler
    data_processor.stopProcessing();

    std::cout << "Main thread exiting." << std::endl;
    return 0;
}
