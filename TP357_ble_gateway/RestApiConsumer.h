// RestApiConsumer.h
#ifndef REST_API_CONSUMER_H
#define REST_API_CONSUMER_H

#include "IDataConsumer.h"
#include "MessageQueue.h"
#include "SensorData.h"
#include "SensorDataSerializer.h" // To use the serialization logic

#include <string>
#include <thread>
#include <atomic>
#include <map>
#include <mutex>
#include <chrono>

// spdlog include
#include "spdlog/spdlog.h"

// libcurl includes (forward declare to avoid full header in .h if possible, but often needed)
#include <curl/curl.h>

/**
 * @brief A data consumer that aggregates sensor data and posts it to a REST API endpoint.
 * This class implements the IDataConsumer interface and runs its own processing loop
 * in a separate thread.
 */
class RestApiConsumer : public IDataConsumer {
public:
    /**
     * @brief Constructs a RestApiConsumer.
     * @param queue Reference to the MessageQueue from which to consume data.
     * @param api_url The URL of the REST API endpoint to post data to.
     * @param logging_window_seconds The duration of the logging window in seconds for this consumer.
     */
    RestApiConsumer(MessageQueue& queue, const std::string& api_url, int logging_window_seconds);

    /**
     * @brief Destroys the RestApiConsumer, ensuring the consumption thread is stopped.
     */
    ~RestApiConsumer();

    /**
     * @brief Starts the data consumption and API posting loop in a new thread.
     */
    void startConsuming() override;

    /**
     * @brief Signals the consumption loop to stop and joins the thread.
     */
    void stopConsuming() override;

private:
    /**
     * @brief The main loop for consuming data and posting to the REST API.
     */
    void consumingLoop();

    /**
     * @brief Helper function to format a std::chrono::system_clock::time_point to a string.
     * @param tp The time_point to format.
     * @return A formatted string (e.g., "YYYY-MM-DDTHH:MM:SSZ").
     */
    std::string formatTimestamp(std::chrono::system_clock::time_point tp);

    MessageQueue& queue_;                        ///< Reference to the message queue
    std::string api_url_;                        ///< The URL of the REST API endpoint
    std::atomic<bool> keep_running_;             ///< Flag to control the consumption loop
    std::thread consuming_thread_;               ///< The thread running the consumption loop

    std::map<std::string, SensorData> latest_samples_in_window_; ///< Stores the latest sample for each sensor in the current window (keyed by MAC address)
    std::mutex latest_samples_mutex_;                   ///< Mutex to protect access to latest_samples_in_window_

    std::chrono::steady_clock::time_point window_start_time_; ///< Timestamp when the current logging window started
    std::chrono::seconds logging_window_duration_;             ///< The configured duration of the logging window
};

#endif // REST_API_CONSUMER_H
