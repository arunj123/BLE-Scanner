    // DataProcessor.h
    #ifndef DATA_PROCESSOR_H
    #define DATA_PROCESSOR_H

    // Ensure all types used in the header are fully defined
    #include "MessageQueue.h"     // MessageQueue is used by value/reference
    #include "IDatabaseManager.h" // IDatabaseManager is used by unique_ptr
    // #include "IFirestoreManager.h" // REMOVED: IFirestoreManager is no longer used

    #include <thread>
    #include <atomic>
    #include <memory> // For std::unique_ptr

    /**
     * @brief Consumes SensorData from a MessageQueue and inserts it into a database.
     * This class runs its processing loop in a separate thread.
     */
    class DataProcessor {
    public:
        /**
         * @brief Constructs a DataProcessor.
         * @param queue Reference to the MessageQueue from which to consume data.
         * @param sqlite_db_manager A unique_ptr to an IDatabaseManager instance for SQLite. Ownership is transferred.
         * // REMOVED: firestore_manager parameter
         */
        DataProcessor(MessageQueue& queue,
                      std::unique_ptr<IDatabaseManager> sqlite_db_manager);

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
        // std::unique_ptr<IFirestoreManager> firestore_manager_; // REMOVED: Pointer to the Firestore manager
        std::atomic<bool> keep_running_;             ///< Flag to control the processing loop
        std::thread processing_thread_;              ///< The thread running the processing loop
    };

    #endif // DATA_PROCESSOR_H
    