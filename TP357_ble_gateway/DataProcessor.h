// DataProcessor.h
#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

// Ensure all types used in the header are fully defined
#include "MessageQueue.h"     // MessageQueue is used by value/reference
#include "IDatabaseManager.h" // IDatabaseManager is used by unique_ptr
#include "SensorData.h"       // SensorData is used in the map
#include "IDataConsumer.h"    // New: Inherit from IDataConsumer

#include <thread>
#include <atomic>
#include <memory> // For std::unique_ptr
#include <optional> // For std::optional (though now used for pop return)
#include <chrono>   // For std::chrono::steady_clock and std::chrono::seconds
#include <map>      // For std::map to store latest samples per sensor
#include <string>   // For map keys

// spdlog include
#include "spdlog/spdlog.h"

/**
 * @brief Consumes SensorData from a MessageQueue, aggregates the latest samples
 * within a configured time window, and inserts the aggregated binary data into a database.
 * This class runs its processing loop in a separate thread.
 */
class DataProcessor : public IDataConsumer { // Inherit from IDataConsumer
public:
    /**
     * @brief Constructs a DataProcessor.
     * @param queue Reference to the MessageQueue from which to consume data.
     * @param sqlite_db_manager A unique_ptr to an IDatabaseManager instance for SQLite. Ownership is transferred.
     * @param logging_window_seconds The duration of the logging window in seconds.
     */
    DataProcessor(MessageQueue& queue,
                  std::unique_ptr<IDatabaseManager> sqlite_db_manager,
                  int logging_window_seconds);

    /**
     * @brief Destroys the DataProcessor, ensuring the processing thread is stopped.
     */
    ~DataProcessor();

    /**
     * @brief Starts the data consumption process. (Implementation of IDataConsumer)
     */
    void startConsuming() override;

    /**
     * @brief Signals the data consumption process to stop and waits for its completion. (Implementation of IDataConsumer)
     */
    void stopConsuming() override;

private:
    /**
     * @brief The main loop for processing data from the queue and inserting into the database.
     */
    void processingLoop();

    /**
     * @brief Helper function to serialize the map of SensorData into a binary vector.
     * @param data_map The map containing SensorData to serialize.
     * @return A vector of characters representing the binary data.
     */
    std::vector<char> serializeSensorDataMap(const std::map<std::string, SensorData>& data_map);

    /**
     * @brief Helper function to format a std::chrono::system_clock::time_point to a string.
     * @param tp The time_point to format.
     * @return A formatted string (e.g., "YYYY-MM-DDTHH:MM:SSZ").
     */
    std::string formatTimestamp(std::chrono::system_clock::time_point tp);


    MessageQueue& queue_;                        ///< Reference to the message queue
    std::unique_ptr<IDatabaseManager> sqlite_db_manager_; ///< Pointer to the SQLite database manager
    std::atomic<bool> keep_running_;             ///< Flag to control the processing loop
    std::thread processing_thread_;              ///< The thread running the processing loop

    std::map<std::string, SensorData> latest_samples_in_window_; ///< Stores the latest sample for each sensor in the current window (keyed by MAC address)
    std::mutex latest_samples_mutex_;                   ///< Mutex to protect access to latest_samples_in_window_

    std::chrono::steady_clock::time_point window_start_time_; ///< Timestamp when the current logging window started
    std::chrono::seconds logging_window_duration_;             ///< The configured duration of the logging window
};

#endif // DATA_PROCESSOR_H
