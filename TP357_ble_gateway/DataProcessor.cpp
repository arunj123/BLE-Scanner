// DataProcessor.cpp
#include "DataProcessor.h"
#include <iostream>
#include <chrono> // For std::chrono::milliseconds, steady_clock
#include <mutex>  // For std::lock_guard
#include <exception> // For std::exception

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
        std::lock_guard<std::mutex> lock(latest_sample_mutex_);
        if (latest_sample_in_window_ && sqlite_db_manager_) {
            std::cout << "[DataProcessor Shutdown] Flushing last collected sample for " << latest_sample_in_window_->predefined_name << " to SQLite." << std::endl;
            sqlite_db_manager_->insertSensorData(*latest_sample_in_window_);
            latest_sample_in_window_.reset(); // Clear it after flushing
        } else if (!latest_sample_in_window_) {
            std::cout << "[DataProcessor Shutdown] No sample to flush on shutdown." << std::endl;
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
    std::cout << "[DataProcessor Loop] Thread has started execution. Thread ID: " << std::this_thread::get_id() << std::endl << std::flush; // Added std::flush
    std::cout << "[DataProcessor Loop] Initial keep_running_ state: " << (keep_running_.load() ? "true" : "false") << std::endl << std::flush; // Added std::flush

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

                // Update the latest sample received in the current window
                std::lock_guard<std::mutex> lock(latest_sample_mutex_);
                latest_sample_in_window_ = received_data;
                std::cout << "[DataProcessor Loop] Updated latest sample for window: " << received_data.predefined_name
                          << " (Temp: " << received_data.temperature << ", Hum: " << received_data.humidity << ", RSSI: " << (int)received_data.rssi << ")" << std::endl << std::flush;

            } else {
                // Timeout occurred on queue.pop(), or queue was empty.
                // This means no new data arrived within the remaining window time.
                // Proceed to check if the window has expired.
            }

            // Check if the current logging window has expired
            now = std::chrono::steady_clock::now(); // Re-check time after potential pop/timeout
            if (now - window_start_time_ >= logging_window_duration_) {
                std::lock_guard<std::mutex> lock(latest_sample_mutex_);
                std::cout << "[DataProcessor Loop] Window expiration check: Current time - Window start time = "
                          << std::chrono::duration_cast<std::chrono::seconds>(now - window_start_time_).count()
                          << "s. Duration: " << logging_window_duration_.count() << "s." << std::endl << std::flush;

                if (latest_sample_in_window_) {
                    // Log the latest sample from the expired window to SQLite
                    if (sqlite_db_manager_) {
                        std::cout << "[DataProcessor Loop] Window expired. Logging latest sample for " << latest_sample_in_window_->predefined_name << " to SQLite." << std::endl << std::flush;
                        sqlite_db_manager_->insertSensorData(*latest_sample_in_window_);
                    } else {
                        std::cerr << "[DataProcessor Loop] SQLite database manager not available. Cannot log." << std::endl << std::flush;
                    }
                    latest_sample_in_window_.reset(); // Clear the latest sample for the next window
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
