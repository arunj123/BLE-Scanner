#include <iostream> // Keep for initial setup messages if spdlog fails
#include <thread>   // For std::thread
#include <chrono>   // For std::chrono::seconds
#include <csignal>  // For std::signal
#include <map>      // For std::map to track connected iTags
#include <mutex>    // For std::mutex to protect shared resources
#include <memory>   // For std::unique_ptr
#include <cstdint>  // For uint64_t
#include <sstream>  // For std::ostringstream
#include <vector>   // For std::vector to manage multiple consumers

// spdlog includes
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h" // For console output with colors

// libcurl global init/cleanup
#include <curl/curl.h>

// Include full definitions of classes used as concrete objects or template arguments first
#include "MessageQueue.h"          // Provides full definition of MessageQueue
#include "SQLiteDatabaseManager.h" // Provides full definition of SQLiteDatabaseManager
#include "DataProcessor.h"         // Provides full definition of DataProcessor
#include "EnvReader.h"             // Provides full definition of EnvReader
#include "BluetoothScanner.h"      // Include the BluetoothScanner class
#include "IDataConsumer.h"         // Interface for data consumers
#include "RestApiConsumer.h"       // NEW: Include the RestApiConsumer

// Global pointers for graceful shutdown
BluetoothScanner* g_scanner_ptr = nullptr;
// Changed to a vector to manage multiple consumers
std::vector<std::unique_ptr<IDataConsumer>> g_consumers; // Use unique_ptr for ownership
std::mutex g_consumers_mutex; // Mutex to protect access to g_consumers

// Signal handler function to gracefully terminate the program
void signal_handler(int signum) {
    spdlog::get("Main")->info("SIGINT received. Initiating graceful shutdown...");

    // Stop all registered consumers first
    std::lock_guard<std::mutex> lock(g_consumers_mutex);
    for (const auto& consumer_ptr : g_consumers) { // Iterate through unique_ptrs
        if (consumer_ptr) {
            consumer_ptr->stopConsuming();
        }
    }

    // Then tell the scanner thread to stop
    if (g_scanner_ptr) {
        g_scanner_ptr->stopScan();
    }
}

int main(int argc, char **argv) {
    // --- Global cURL Initialization (Do this once at application start) ---
    CURLcode curl_global_res = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_global_res != CURLE_OK) {
        std::cerr << "curl_global_init() failed: " << curl_easy_strerror(curl_global_res) << std::endl;
        return 1;
    }

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
        spdlog::create<spdlog::sinks::stdout_color_sink_mt>("SensorDataSerializer");
        auto rest_api_logger = spdlog::create<spdlog::sinks::stdout_color_sink_mt>("RestApiConsumer");
        rest_api_logger->set_level(spdlog::level::trace); // Set RestApiConsumer to TRACE for maximum verbosity

        spdlog::get("Main")->info("spdlog initialized successfully.");

    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "spdlog initialization failed: " << ex.what() << std::endl;
        curl_global_cleanup(); // Clean up curl if spdlog fails
        return 1;
    }
    // --- End spdlog Initialization ---

    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);

    // --- Read settings from .env file ---
    EnvReader env_reader;
    if (!env_reader.load(".env")) {
        spdlog::get("EnvReader")->error("Could not load .env file. Using default settings.");
    }

    // Get logging window duration from .env or use a default (e.g., 5 seconds for testing)
    int logging_window_seconds = std::stoi(env_reader.getOrDefault("LOGGING_WINDOW_SECONDS", "5")); // Changed to 5s for easier testing
    spdlog::get("Main")->info("Configured logging window: {} seconds.", logging_window_seconds);

    // Get REST API URL from .env or use a default
    std::string rest_api_url = env_reader.getOrDefault("REST_API_URL", "http://localhost:3000/sensor-data");
    spdlog::get("Main")->info("Configured REST API URL: {}", rest_api_url);


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
        curl_global_cleanup(); // Clean up curl if scanner fails
        return 1;
    }

    // --- Data Consumers Setup ---
    // 1. SQLite Data Processor
    auto sqlite_db_manager = std::make_unique<SQLiteDatabaseManager>();
    if (!sqlite_db_manager->initialize("sensor_readings.db")) {
        spdlog::get("SQLiteDatabaseManager")->error("Failed to initialize SQLite database. Exiting.");
        scanner.stopScan();
        curl_global_cleanup(); // Clean up curl if DB fails
        return 1;
    }
    g_consumers.push_back(std::make_unique<DataProcessor>(sensor_data_queue, std::move(sqlite_db_manager), logging_window_seconds));

    // 2. REST API Consumer
    g_consumers.push_back(std::make_unique<RestApiConsumer>(sensor_data_queue, rest_api_url, logging_window_seconds));

    // Start all data consumers
    spdlog::get("Main")->info("Attempting to start data consumers...");
    {
        std::lock_guard<std::mutex> lock(g_consumers_mutex); // Lock while starting consumers
        for (const auto& consumer_ptr : g_consumers) {
            consumer_ptr->startConsuming();
        }
    }
    spdlog::get("Main")->info("Data consumers start attempt complete.");
    // --- End Data Consumers Setup ---


    // Start the scanning loop in a separate thread
    spdlog::get("Main")->info("Attempting to start BluetoothScanner...");
    std::thread scanner_thread(&BluetoothScanner::startScan, &scanner);
    spdlog::get("Main")->info("BluetoothScanner start attempt complete.");


    // Main thread waits for the scanner thread to finish
    scanner_thread.join();

    // Ensure all data consumers also finish after scanner, if not already stopped by signal handler
    spdlog::get("Main")->info("Ensuring all data consumers are stopped...");
    {
        std::lock_guard<std::mutex> lock(g_consumers_mutex); // Lock while stopping consumers
        // The signal handler should have already called stopConsuming, but this ensures it.
        // It's safe to call stopConsuming multiple times (it checks if thread is joinable).
        for (const auto& consumer_ptr : g_consumers) {
            if (consumer_ptr) {
                consumer_ptr->stopConsuming();
            }
        }
        g_consumers.clear(); // Clear the vector of unique_ptrs
    }

    spdlog::get("Main")->info("Main thread exiting.");
    curl_global_cleanup(); // Global cURL cleanup (Do this once at application end)
    return 0;
}
