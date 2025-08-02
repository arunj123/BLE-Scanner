// DataProcessor.cpp
#include "DataProcessor.h"
#include <iostream>
#include <chrono> // For std::chrono::milliseconds, steady_clock, system_clock
#include <mutex>  // For std::lock_guard
#include <exception> // For std::exception
#include <iomanip> // For std::put_time
#include <sstream> // For std::stringstream

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
    // Constructor initializes members. Thread is started by startProcessing().
    // window_start_time_ is initialized in processingLoop.
}

/**
 * @brief Destroys the DataProcessor, ensuring the processing thread is stopped.
 */
DataProcessor::~DataProcessor() {
    stopProcessing(); // Ensure the thread is joined before destruction
}

/**
 * @brief Starts the data processing loop in a new thread.
 */
void DataProcessor::startProcessing() {
    if (processing_thread_.joinable()) {
        std::cerr << "DataProcessor already running." << std::endl;
        return;
    }
    // Start the processingLoop in a new thread
    processing_thread_ = std::thread(&DataProcessor::processingLoop, this);
    std::cout << "DataProcessor started. Thread ID: " << processing_thread_.get_id() << std::endl; // Added thread ID

    if (processing_thread_.joinable()) {
        std::cout << "DataProcessor thread is joinable (successfully created)." << std::endl;
    } else {
        std::cerr << "ERROR: DataProcessor thread is NOT joinable after creation. Thread creation might have failed." << std::endl;
    }
}

/**
 * @brief Signals the processing loop to stop and joins the thread.
 */
void DataProcessor::stopProcessing() {
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
            std::vector<char> binary_data = serializeSensorDataMap(latest_samples_in_window_);
            std::cout << "[DataProcessor Shutdown] Flushing last collected aggregated sample (count: " << latest_samples_in_window_.size() << ") for timestamp " << timestamp_str << " to SQLite." << std::endl;
            sqlite_db_manager_->insertAggregatedSensorData(timestamp_str, binary_data);
            latest_samples_in_window_.clear(); // Clear it after flushing
        } else if (latest_samples_in_window_.empty()) {
            std::cout << "[DataProcessor Shutdown] No samples to flush on shutdown." << std::endl;
        } else {
            std::cout << "[DataProcessor Shutdown] SQLite manager not available, no flush." << std::endl;
        }
        // --- End Flush ---

        std::cout << "DataProcessor stopped." << std::endl;
    }
}

/**
 * @brief The main loop for processing data from the queue and inserting into the database.
 */
void DataProcessor::processingLoop() {
    // Add an immediate print statement here to confirm thread execution
    std::cout << "[DataProcessor Loop] Thread has started execution. Thread ID: " << std::this_thread::get_id() << std::endl << std::flush;
    std::cout << "[DataProcessor Loop] Initial keep_running_ state: " << (keep_running_.load() ? "true" : "false") << std::endl << std::flush;

    try {
        std::cout << "[DataProcessor Loop] Entered. Logging window: " << logging_window_duration_.count() << " seconds." << std::endl << std::flush;

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
                    std::cout << "[DataProcessor Loop] Received shutdown signal in processing loop. Breaking." << std::endl << std::flush;
                    break; // Exit the loop
                }

                // Update the latest sample received in the current window for its MAC address
                std::lock_guard<std::mutex> lock(latest_samples_mutex_); // Use latest_samples_mutex_
                latest_samples_in_window_[received_data.mac_address] = received_data;
                std::cout << "[DataProcessor Loop] Updated latest sample for MAC: " << received_data.mac_address
                          << " (Name: " << received_data.predefined_name << ", Temp: " << received_data.temperature << ", Hum: " << received_data.humidity << ", RSSI: " << (int)received_data.rssi << ")" << std::endl << std::flush;

            } else {
                // Timeout occurred on queue.pop(), or queue was empty.
                // This means no new data arrived within the remaining window time.
                // Proceed to check if the window has expired.
            }

            // Check if the current logging window has expired
            now = std::chrono::steady_clock::now(); // Re-check time after potential pop/timeout
            if (now - window_start_time_ >= logging_window_duration_) {
                std::lock_guard<std::mutex> lock(latest_samples_mutex_); // Use latest_samples_mutex_
                std::cout << "[DataProcessor Loop] Window expiration check: Current time - Window start time = "
                          << std::chrono::duration_cast<std::chrono::seconds>(now - window_start_time_).count()
                          << "s. Duration: " << logging_window_duration_.count() << "s." << std::endl << std::flush;

                if (!latest_samples_in_window_.empty()) { // Check if map is not empty
                    // Get current timestamp for this aggregated entry
                    std::string timestamp_str = formatTimestamp(std::chrono::system_clock::now());
                    std::vector<char> binary_data = serializeSensorDataMap(latest_samples_in_window_);

                    // Log the aggregated data from the expired window to SQLite
                    if (sqlite_db_manager_) {
                        std::cout << "[DataProcessor Loop] Window expired. Logging aggregated sample (count: " << latest_samples_in_window_.size() << ") for timestamp " << timestamp_str << " to SQLite." << std::endl << std::flush;
                        sqlite_db_manager_->insertAggregatedSensorData(timestamp_str, binary_data);
                    } else {
                        std::cerr << "[DataProcessor Loop] SQLite database manager not available. Cannot log aggregated data." << std::endl << std::flush;
                    }
                    latest_samples_in_window_.clear(); // Clear the map for the next window
                } else {
                    std::cout << "[DataProcessor Loop] Window expired, but no samples received in this window. Not logging." << std::endl << std::flush;
                }
                // Start a new window
                window_start_time_ = now;
                std::cout << "[DataProcessor Loop] New logging window started." << std::endl << std::flush;
            }
        }
        std::cout << "[DataProcessor Loop] Exited." << std::endl << std::flush;
    } catch (const std::exception& e) {
        std::cerr << "[DataProcessor Loop] FATAL ERROR: Unhandled exception: " << e.what() << std::endl << std::flush;
    } catch (...) {
        std::cerr << "[DataProcessor Loop] FATAL ERROR: Unknown unhandled exception." << std::endl << std::flush;
    }
}

/**
 * @brief Helper function to serialize the map of SensorData into a binary vector.
 * @param data_map The map containing SensorData to serialize.
 * @return A vector of characters representing the binary data.
 */
std::vector<char> DataProcessor::serializeSensorDataMap(const std::map<std::string, SensorData>& data_map) {
    std::vector<char> buffer;
    // Reserve some space to reduce reallocations (estimation)
    buffer.reserve(data_map.size() * (1 + 18 + 1 + 30 + 1 + 30 + sizeof(double) + sizeof(double) + 1 + 25)); // Rough estimate

    // Write total number of sensors in this aggregation (1 byte)
    uint8_t num_sensors = static_cast<uint8_t>(data_map.size());
    buffer.push_back(num_sensors);

    for (const auto& pair : data_map) {
        const SensorData& data = pair.second;

        // MAC Address
        uint8_t mac_len = static_cast<uint8_t>(data.mac_address.length());
        buffer.push_back(mac_len);
        for (char c : data.mac_address) buffer.push_back(c);

        // Predefined Name
        uint8_t predefined_name_len = static_cast<uint8_t>(data.predefined_name.length());
        buffer.push_back(predefined_name_len);
        for (char c : data.predefined_name) buffer.push_back(c);

        // Decoded Device Name
        uint8_t decoded_device_name_len = static_cast<uint8_t>(data.decoded_device_name.length());
        buffer.push_back(decoded_device_name_len);
        for (char c : data.decoded_device_name) buffer.push_back(c);

        // Temperature (double)
        const char* temp_bytes = reinterpret_cast<const char*>(&data.temperature);
        for (size_t i = 0; i < sizeof(double); ++i) buffer.push_back(temp_bytes[i]);

        // Humidity (double)
        const char* hum_bytes = reinterpret_cast<const char*>(&data.humidity);
        for (size_t i = 0; i < sizeof(double); ++i) buffer.push_back(hum_bytes[i]);

        // RSSI (int8_t)
        buffer.push_back(static_cast<char>(data.rssi));

        // Note: SensorData's timestamp is not serialized here, as the aggregated row has its own TIMESTAMP.
        // If individual sensor timestamps within the window are needed, they would be added here.
    }
    return buffer;
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
