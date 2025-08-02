// DataProcessor.h
#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

// Ensure all types used in the header are fully defined
#include "MessageQueue.h"     // MessageQueue is used by value/reference
#include "IDatabaseManager.h" // IDatabaseManager is used by unique_ptr

#include <thread>
#include <atomic>
#include <memory> // For std::unique_ptr
#include <optional> // For std::optional<SensorData>
#include <chrono>   // For std::chrono::steady_clock and std::chrono::seconds

/**
 * @brief Consumes SensorData from a MessageQueue and inserts the latest sample
 * within a configured time window into a database.
 * This class runs its processing loop in a separate thread.
 */
class DataProcessor {
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
     * @brief Starts the data processing loop in a new thread.
     */
    void startProcessing();

    /**
     * @brief Signals the processing loop to stop and joins the thread.
     */
    void stopProcessing();

private:
    /**
     * @brief The main loop for processing data from the queue and inserting into the database.
     */
    void processingLoop();

    MessageQueue& queue_;                        ///< Reference to the message queue
    std::unique_ptr<IDatabaseManager> sqlite_db_manager_; ///< Pointer to the SQLite database manager
    std::atomic<bool> keep_running_;             ///< Flag to control the processing loop
    std::thread processing_thread_;              ///< The thread running the processing loop

    std::optional<SensorData> latest_sample_in_window_; ///< Stores the latest sample received in the current window
    std::mutex latest_sample_mutex_;                   ///< Mutex to protect access to latest_sample_in_window_

    std::chrono::steady_clock::time_point window_start_time_; ///< Timestamp when the current logging window started
    std::chrono::seconds logging_window_duration_;             ///< The configured duration of the logging window
};

#endif // DATA_PROCESSOR_H
