#include <iostream> // Keep for initial setup messages if spdlog fails
#include <thread>   // For std::thread
#include <chrono>   // For std::chrono::seconds
#include <csignal>  // For std::signal
#include <map>      // For std::map to track connected iTags
#include <mutex>    // For std::mutex to protect shared resources
#include <memory>   // For std::unique_ptr
#include <cstdint>  // For uint64_t (though not directly used for thread ID now)
#include <sstream>  // For std::ostringstream

// spdlog includes
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h" // For console output with colors

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
    spdlog::get("Main")->info("SIGINT received. Initiating graceful shutdown...");
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
    // --- spdlog Initialization ---
    try {
        // Create a color-enabled console sink
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"); // Custom pattern with logger name

        // Create a default logger and register it
        auto main_logger = std::make_shared<spdlog::logger>("Main", console_sink);
        spdlog::register_logger(main_logger);

        // Set global log level (e.g., info, debug, trace)
        spdlog::set_level(spdlog::level::info); // Set to info for production, debug for development
        spdlog::flush_on(spdlog::level::warn); // Flush logs immediately on warning or higher

        // Create other named loggers for different components
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("BluetoothScanner");
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("TP357Handler");
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("MessageQueue");
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("DataProcessor");
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("SQLiteDatabaseManager");
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("EnvReader");

        spdlog::get("Main")->info("spdlog initialized successfully.");

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "spdlog initialization failed: " << ex.what() << std::endl;
        return 1; // Exit if logging cannot be set up
    }
    // --- End spdlog Initialization ---

    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);

    // --- Read settings from .env file ---
    EnvReader env_reader;
    if (!env_reader.load(".env")) {
        spdlog::get("EnvReader")->error("Could not load .env file. Using default settings.");
    }

    // Get logging window duration from .env or use a default (e.g., 20 seconds)
    int logging_window_seconds = std::stoi(env_reader.getOrDefault("LOGGING_WINDOW_SECONDS", "20")); // Changed to 20s
    spdlog::get("Main")->info("Configured logging window: {} seconds.", logging_window_seconds);

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
        spdlog::get("BluetoothScanner")->error("Failed to initialize Bluetooth scanner. Exiting.");
        return 1;
    }

    // Create and initialize the SQLite database manager
    auto sqlite_db_manager = std::make_unique<SQLiteDatabaseManager>();
    // Note: The database file will now contain the 'sensor_readings_aggregated' table.
    // If you want to clear old 'sensor_readings' table, you might need to delete the .db file manually.
    if (!sqlite_db_manager->initialize("sensor_readings.db")) {
        spdlog::get("SQLiteDatabaseManager")->error("Failed to initialize SQLite database. Exiting.");
        scanner.stopScan(); // Ensure scanner is stopped if DB init fails
        return 1;
    }

    // --- Start DataProcessor BEFORE Scanner ---
    spdlog::get("Main")->info("Attempting to start DataProcessor...");
    DataProcessor data_processor(sensor_data_queue, std::move(sqlite_db_manager), logging_window_seconds);
    g_data_processor_ptr = &data_processor; // Assign global data processor pointer
    data_processor.startProcessing();
    spdlog::get("Main")->info("DataProcessor start attempt complete.");
    // --- End DataProcessor Start ---


    // Start the scanning loop in a separate thread
    spdlog::get("Main")->info("Attempting to start BluetoothScanner...");
    std::thread scanner_thread(&BluetoothScanner::startScan, &scanner);
    spdlog::get("Main")->info("BluetoothScanner start attempt complete.");


    // Main thread waits for the scanner thread to finish
    // (which happens when Ctrl+C is pressed and stopScan() is called).
    scanner_thread.join();

    // Ensure data processor also finishes after scanner, if not already stopped by signal handler
    data_processor.stopProcessing();

    spdlog::get("Main")->info("Main thread exiting.");
    return 0;
}
