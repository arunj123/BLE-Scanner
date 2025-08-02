// DataProcessor.cpp
#include "DataProcessor.h"
#include "SensorDataSerializer.h" // Include the new serializer
#include <chrono> // For std::chrono::milliseconds, steady_clock, system_clock
#include <mutex>  // For std::lock_guard
#include <exception> // For std::exception
#include <iomanip> // For std::put_time
#include <sstream> // For std::stringstream
#include <cstdint> // For uint64_t, uint8_t
#include <cstdio>  // For sscanf
#include <array>   // For std::array

// spdlog include
#include "spdlog/spdlog.h"

/**
 * @brief Constructs a DataProcessor.
 * @param queue Reference to the MessageQueue from which to consume data.
 * @param sqlite_db_manager A unique_ptr to an IDatabaseManager instance for SQLite. Ownership is transferred.
 * @param logging_window_seconds The duration of the logging window in seconds.
 */
DataProcessor::DataProcessor(MessageQueue& queue,
                             std::unique_ptr<IDatabaseManager> sqlite_db_manager,
                             int logging_window_seconds)
    : queue_(queue),
      sqlite_db_manager_(std::move(sqlite_db_manager)),
      keep_running_(true), // This is initialized to true
      logging_window_duration_(logging_window_seconds) {
    // Constructor initializes members. Thread is started by startConsuming().
    // window_start_time_ is initialized in processingLoop.
}

/**
 * @brief Destroys the DataProcessor, ensuring the processing thread is stopped.
 */
DataProcessor::~DataProcessor() {
    stopConsuming(); // Ensure the thread is joined before destruction
}

/**
 * @brief Starts the data consumption process. (Implementation of IDataConsumer)
 */
void DataProcessor::startConsuming() {
    if (processing_thread_.joinable()) {
        spdlog::get("DataProcessor")->error("DataProcessor already running.");
        return;
    }
    // Start the processingLoop in a new thread
    processing_thread_ = std::thread(&DataProcessor::processingLoop, this);

    // Convert std::thread::id to string for spdlog compatibility
    std::ostringstream oss;
    oss << processing_thread_.get_id();
    spdlog::get("DataProcessor")->info("DataProcessor started. Thread ID: {}", oss.str());

    if (processing_thread_.joinable()) {
        spdlog::get("DataProcessor")->info("DataProcessor thread is joinable (successfully created).");
    } else {
        spdlog::get("DataProcessor")->error("ERROR: DataProcessor thread is NOT joinable after creation. Thread creation might have failed.");
    }
}

/**
 * @brief Signals the data consumption process to stop and waits for its completion. (Implementation of IDataConsumer)
 */
void DataProcessor::stopConsuming() {
    if (processing_thread_.joinable()) {
        keep_running_.store(false); // Signal the loop to stop
        // Push a dummy item to unblock the queue if the processingLoop is currently
        // blocked on queue_.pop(). This ensures a timely shutdown.
        queue_.push(SensorData()); // Use default constructor for dummy data
        processing_thread_.join(); // Wait for the thread to finish

        // --- IMPORTANT: Flush any pending latest sample before final shutdown ---
        std::lock_guard<std::mutex> lock(latest_samples_mutex_); // Use latest_samples_mutex_
        if (!latest_samples_in_window_.empty() && sqlite_db_manager_) { // Check if map is not empty
            std::string timestamp_str = formatTimestamp(std::chrono::system_clock::now());
            std::vector<char> binary_data = SensorDataSerializer::serializeSensorDataMap(latest_samples_in_window_); // Use serializer
            spdlog::get("DataProcessor")->info("[Shutdown] Flushing last collected aggregated sample (count: {}) for timestamp {} to SQLite.", latest_samples_in_window_.size(), timestamp_str);
            sqlite_db_manager_->insertAggregatedSensorData(timestamp_str, binary_data);
            latest_samples_in_window_.clear(); // Clear it after flushing
        } else if (latest_samples_in_window_.empty()) {
            spdlog::get("DataProcessor")->info("[Shutdown] No samples to flush on shutdown.");
        } else {
            spdlog::get("DataProcessor")->warn("[Shutdown] SQLite manager not available, no flush.");
        }
        // --- End Flush ---

        spdlog::get("DataProcessor")->info("DataProcessor stopped.");
    }
}

/**
 * @brief The main loop for processing data from the queue and inserting into the database.
 */
void DataProcessor::processingLoop() {
    // Convert std::this_thread::get_id() to string for spdlog compatibility
    std::ostringstream oss_loop_id;
    oss_loop_id << std::this_thread::get_id();
    spdlog::get("DataProcessor")->info("[Loop] Thread has started execution. Thread ID: {}", oss_loop_id.str());
    spdlog::get("DataProcessor")->info("[Loop] Initial keep_running_ state: {}", (keep_running_.load() ? "true" : "false"));

    try {
        spdlog::get("DataProcessor")->info("[Loop] Entered. Logging window: {} seconds.", logging_window_duration_.count());

        window_start_time_ = std::chrono::steady_clock::now(); // Initialize the first window start time

        while (keep_running_.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_in_window = std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start_time_);
            auto remaining_time = logging_window_duration_ - elapsed_in_window;

            // If the window has already expired or is very short, set a minimal timeout to process
            if (remaining_time <= std::chrono::milliseconds(0)) {
                remaining_time = std::chrono::milliseconds(1); // Ensure a tiny wait to prevent busy-loop if no data
            }

            std::optional<SensorData> data_opt = queue_.pop(remaining_time); // Wait for data with a timeout

            if (data_opt) {
                SensorData received_data = *data_opt;

                // Check if this is the dummy shutdown signal.
                if (!keep_running_.load() && received_data.mac_address.empty() && received_data.predefined_name.empty() && received_data.decoded_device_name.empty()) {
                    spdlog::get("DataProcessor")->info("[Loop] Received shutdown signal in processing loop. Breaking.");
                    break; // Exit the loop
                }

                // Update the latest sample received in the current window for its MAC address
                std::lock_guard<std::mutex> lock(latest_samples_mutex_); // Use latest_samples_mutex_
                latest_samples_in_window_[received_data.mac_address] = received_data;
                spdlog::get("DataProcessor")->info("[Loop] Updated latest sample for MAC: {} (Name: {}, Temp: {}, Hum: {}, RSSI: {})",
                                                  received_data.mac_address, received_data.predefined_name,
                                                  received_data.temperature, received_data.humidity, (int)received_data.rssi);

            } else {
                // Timeout occurred on queue.pop(), or queue was empty.
                // This means no new data arrived within the remaining window time.
                // Proceed to check if the window has expired.
            }

            // Check if the current logging window has expired
            now = std::chrono::steady_clock::now(); // Re-check time after potential pop/timeout
            if (now - window_start_time_ >= logging_window_duration_) {
                std::lock_guard<std::mutex> lock(latest_samples_mutex_); // Use latest_samples_mutex_
                spdlog::get("DataProcessor")->info("[Loop] Window expiration check: Current time - Window start time = {}s. Duration: {}s.",
                                                  std::chrono::duration_cast<std::chrono::seconds>(now - window_start_time_).count(),
                                                  logging_window_duration_.count());

                if (!latest_samples_in_window_.empty()) { // Check if map is not empty
                    // Get current timestamp for this aggregated entry
                    std::string timestamp_str = formatTimestamp(std::chrono::system_clock::now());
                    std::vector<char> binary_data = SensorDataSerializer::serializeSensorDataMap(latest_samples_in_window_); // Use serializer

                    // Log the aggregated data from the expired window to SQLite
                    if (sqlite_db_manager_) {
                        spdlog::get("DataProcessor")->info("[Loop] Window expired. Logging aggregated sample (count: {}) for timestamp {} to SQLite.", latest_samples_in_window_.size(), timestamp_str);
                        sqlite_db_manager_->insertAggregatedSensorData(timestamp_str, binary_data);
                    } else {
                        spdlog::get("DataProcessor")->error("[Loop] SQLite database manager not available. Cannot log aggregated data.");
                    }
                    latest_samples_in_window_.clear(); // Clear the map for the next window
                } else {
                    spdlog::get("DataProcessor")->info("[Loop] Window expired, but no samples received in this window. Not logging.");
                }
                // Start a new window
                window_start_time_ = now;
                spdlog::get("DataProcessor")->info("[Loop] New logging window started.");
            }
        }
        spdlog::get("DataProcessor")->info("[Loop] Exited.");
    } catch (const std::exception& e) {
        spdlog::get("DataProcessor")->critical("[Loop] FATAL ERROR: Unhandled exception: {}", e.what());
    } catch (...) {
        spdlog::get("DataProcessor")->critical("[Loop] FATAL ERROR: Unknown unhandled exception.");
    }
}

/**
 * @brief Helper function to format a std::chrono::system_clock::time_point to a string.
 * @param tp The time_point to format.
 * @return A formatted string (e.g., "YYYY-MM-DDTHH:MM:SSZ").
 */
std::string DataProcessor::formatTimestamp(std::chrono::system_clock::time_point tp) {
    // Convert to time_t
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);

    // Convert to tm struct (UTC)
    std::tm tm_utc;
    #ifdef _WIN32
        gmtime_s(&tm_utc, &tt); // Windows specific
    #else
        gmtime_r(&tt, &tm_utc); // POSIX specific
    #endif

    // Format the time
    std::stringstream ss;
    ss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}
