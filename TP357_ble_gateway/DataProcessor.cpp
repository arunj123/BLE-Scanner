    // DataProcessor.cpp
    #include "DataProcessor.h"
    #include <iostream>
    #include <chrono> // For std::chrono::milliseconds

    /**
     * @brief Constructs a DataProcessor.
     * @param queue Reference to the MessageQueue from which to consume data.
     * @param sqlite_db_manager A unique_ptr to an IDatabaseManager instance for SQLite. Ownership is transferred.
     * // REMOVED: firestore_manager parameter
     */
    DataProcessor::DataProcessor(MessageQueue& queue,
                                 std::unique_ptr<IDatabaseManager> sqlite_db_manager)
        : queue_(queue),
          sqlite_db_manager_(std::move(sqlite_db_manager)),
          // firestore_manager_(std::move(firestore_manager)), // REMOVED
          keep_running_(true) {
        // Constructor initializes members. Thread is started by startProcessing().
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
        std::cout << "DataProcessor started." << std::endl;
    }

    /**
     * @brief Signals the processing loop to stop and joins the thread.
     */
    void DataProcessor::stopProcessing() {
        if (processing_thread_.joinable()) {
            keep_running_.store(false); // Signal the loop to stop
            // Push a dummy item to unblock the queue if the processingLoop is currently
            // blocked on queue_.pop(). This ensures a timely shutdown.
            // The dummy data is checked in processingLoop to avoid processing it.
            queue_.push(SensorData()); // Use default constructor for dummy data
            processing_thread_.join(); // Wait for the thread to finish
            std::cout << "DataProcessor stopped." << std::endl;
        }
    }

    /**
     * @brief The main loop for processing data from the queue and inserting into the database.
     */
    void DataProcessor::processingLoop() {
        std::cout << "Data processing loop entered." << std::endl;
        while (keep_running_.load()) {
            std::optional<SensorData> data_opt = queue_.pop(); // Blocks until data is available or signaled

            if (data_opt) {
                SensorData data = *data_opt;

                // Check if this is the dummy shutdown signal.
                if (!keep_running_.load() && data.mac_address.empty() && data.predefined_name.empty() && data.decoded_device_name.empty()) {
                    std::cout << "Received shutdown signal in processing loop." << std::endl;
                    break; // Exit the loop
                }

                // bool inserted_to_firestore = false; // REMOVED
                // // Attempt to insert to Firestore if online and manager is available // REMOVED
                // if (firestore_manager_ && firestore_manager_->isOnline()) { // REMOVED
                //     std::cout << "Attempting to insert data for " << data.predefined_name << " to Firestore..." << std::endl; // REMOVED
                //     if (firestore_manager_->insertSensorData(data)) { // REMOVED
                //         std::cout << "Successfully inserted data for " << data.predefined_name << " to Firestore." << std::endl; // REMOVED
                //         inserted_to_firestore = true; // REMOVED
                //     } else { // REMOVED
                //         std::cerr << "Failed to insert data for " << data.predefined_name << " to Firestore. Falling back to SQLite." << std::endl; // REMOVED
                //     } // REMOVED
                // } else { // REMOVED
                //     std::cout << "Firestore is offline or not configured. Falling back to SQLite." << std::endl; // REMOVED
                // } // REMOVED

                // If Firestore insertion failed or was skipped, try SQLite // MODIFIED: Always try SQLite
                // if (!inserted_to_firestore) { // REMOVED
                    if (sqlite_db_manager_) {
                        // SQLiteDatabaseManager::insertSensorData already prints success/failure
                        sqlite_db_manager_->insertSensorData(data);
                    } else {
                        std::cerr << "SQLite database manager not available in DataProcessor." << std::endl;
                    }
                // } // REMOVED
            }
            // Small sleep to prevent busy-waiting if the queue is frequently empty
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "Data processing loop exited." << std::endl;
    }
    