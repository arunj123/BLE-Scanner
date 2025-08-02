// RestApiConsumer.cpp
#include "RestApiConsumer.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept> // For std::runtime_error

// spdlog include
#include "spdlog/spdlog.h"

// Callback function for libcurl to write received data (not used for POST, but good practice)
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

/**
 * @brief Constructs a RestApiConsumer.
 * @param queue Reference to the MessageQueue from which to consume data.
 * @param api_url The URL of the REST API endpoint to post data to.
 * @param logging_window_seconds The duration of the logging window in seconds for this consumer.
 */
RestApiConsumer::RestApiConsumer(MessageQueue& queue, const std::string& api_url, int logging_window_seconds)
    : queue_(queue),
      api_url_(api_url),
      keep_running_(true),
      logging_window_duration_(logging_window_seconds) {
    // Constructor initializes members. Thread is started by startConsuming().
    // window_start_time_ is initialized in consumingLoop.
}

/**
 * @brief Destroys the RestApiConsumer, ensuring the consumption thread is stopped.
 */
RestApiConsumer::~RestApiConsumer() {
    stopConsuming(); // Ensure the thread is joined before destruction
}

/**
 * @brief Starts the data consumption and API posting loop in a new thread.
 */
void RestApiConsumer::startConsuming() {
    if (consuming_thread_.joinable()) {
        spdlog::get("RestApiConsumer")->error("RestApiConsumer already running.");
        return;
    }
    consuming_thread_ = std::thread(&RestApiConsumer::consumingLoop, this);
    std::ostringstream oss;
    oss << consuming_thread_.get_id();
    spdlog::get("RestApiConsumer")->info("RestApiConsumer started. Thread ID: {}", oss.str());

    if (consuming_thread_.joinable()) {
        spdlog::get("RestApiConsumer")->info("RestApiConsumer thread is joinable (successfully created).");
    } else {
        spdlog::get("RestApiConsumer")->error("ERROR: RestApiConsumer thread is NOT joinable after creation. Thread creation might have failed.");
    }
}

/**
 * @brief Signals the consumption loop to stop and joins the thread.
 */
void RestApiConsumer::stopConsuming() {
    if (consuming_thread_.joinable()) {
        spdlog::get("RestApiConsumer")->trace("stopConsuming called. Setting keep_running_ to false.");
        keep_running_.store(false); // Signal the loop to stop
        // Push a dummy item to unblock the queue if the consumingLoop is currently
        // blocked on queue_.pop(). This ensures a timely shutdown.
        spdlog::get("RestApiConsumer")->trace("Pushing dummy SensorData to queue to unblock.");
        queue_.push(SensorData()); // Use default constructor for dummy data
        spdlog::get("RestApiConsumer")->trace("Joining consuming thread.");
        consuming_thread_.join(); // Wait for the thread to finish
        spdlog::get("RestApiConsumer")->trace("Consuming thread joined.");

        // --- IMPORTANT: Flush any pending latest sample before final shutdown ---
        std::lock_guard<std::mutex> lock(latest_samples_mutex_);
        if (!latest_samples_in_window_.empty()) {
            std::string timestamp_str = formatTimestamp(std::chrono::system_clock::now());
            std::vector<char> binary_data = SensorDataSerializer::serializeSensorDataMap(latest_samples_in_window_);
            spdlog::get("RestApiConsumer")->info("[Shutdown] Flushing last collected aggregated sample (count: {}) for timestamp {} to REST API.", latest_samples_in_window_.size(), timestamp_str);
            // Attempt to send the last batch
            CURL *curl = curl_easy_init(); // Get a curl handle
            std::string readBuffer; // To store response from the server

            if (curl) {
                spdlog::get("RestApiConsumer")->trace("[Shutdown] cURL handle initialized for flush.");
                curl_easy_setopt(curl, CURLOPT_URL, api_url_.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, binary_data.data());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, binary_data.size());
                curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L); // 5 second timeout for shutdown flush

                struct curl_slist *headers = NULL;
                headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

                spdlog::get("RestApiConsumer")->trace("[Shutdown] Performing cURL POST for flush...");
                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    spdlog::get("RestApiConsumer")->error("[Shutdown] curl_easy_perform() failed: {}", curl_easy_strerror(res));
                } else {
                    long http_code = 0;
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                    spdlog::get("RestApiConsumer")->info("[Shutdown] Successfully sent aggregated data to REST API. HTTP Status: {}, Response: {}", http_code, readBuffer);
                }
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
                spdlog::get("RestApiConsumer")->trace("[Shutdown] cURL handle cleaned up for flush.");
            } else {
                spdlog::get("RestApiConsumer")->error("[Shutdown] Failed to initialize curl for shutdown flush.");
            }
            latest_samples_in_window_.clear(); // Clear it after flushing
            spdlog::get("RestApiConsumer")->trace("[Shutdown] latest_samples_in_window_ cleared.");
        } else {
            spdlog::get("RestApiConsumer")->info("[Shutdown] No samples to flush on shutdown.");
        }
        // --- End Flush ---

        spdlog::get("RestApiConsumer")->info("RestApiConsumer stopped.");
    } else {
        spdlog::get("RestApiConsumer")->info("stopConsuming called, but thread is not joinable (already stopped or never started).");
    }
}

/**
 * @brief The main loop for consuming data and posting to the REST API.
 */
void RestApiConsumer::consumingLoop() {
    std::ostringstream oss_loop_id;
    oss_loop_id << std::this_thread::get_id();
    spdlog::get("RestApiConsumer")->info("[Loop] Thread has started execution. Thread ID: {}", oss_loop_id.str());
    spdlog::get("RestApiConsumer")->info("[Loop] Initial keep_running_ state: {}", (keep_running_.load() ? "true" : "false"));

    try {
        spdlog::get("RestApiConsumer")->info("[Loop] Entered. API URL: {}, Logging window: {} seconds.", api_url_, logging_window_duration_.count());

        window_start_time_ = std::chrono::steady_clock::now(); // Initialize the first window start time
        spdlog::get("RestApiConsumer")->trace("[Loop] Initial window_start_time_ set.");

        while (keep_running_.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_in_window = std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start_time_);
            auto remaining_time = logging_window_duration_ - elapsed_in_window;

            spdlog::get("RestApiConsumer")->trace("[Loop] Iteration start. Elapsed: {}ms, Remaining: {}ms.", elapsed_in_window.count(), remaining_time.count());

            if (remaining_time <= std::chrono::milliseconds(0)) {
                remaining_time = std::chrono::milliseconds(1);
                spdlog::get("RestApiConsumer")->trace("[Loop] Remaining time <= 0, setting to 1ms.");
            }

            spdlog::get("RestApiConsumer")->trace("[Loop] Calling queue.pop with timeout {}ms.", remaining_time.count());
            std::optional<SensorData> data_opt = queue_.pop(remaining_time);
            spdlog::get("RestApiConsumer")->trace("[Loop] queue.pop returned.");

            if (data_opt) {
                SensorData received_data = *data_opt;
                spdlog::get("RestApiConsumer")->trace("[Loop] Data received from queue: MAC {}", received_data.mac_address);

                if (!keep_running_.load() && received_data.mac_address.empty() && received_data.predefined_name.empty() && received_data.decoded_device_name.empty()) {
                    spdlog::get("RestApiConsumer")->info("[Loop] Received shutdown signal in consuming loop. Breaking.");
                    break;
                }

                std::lock_guard<std::mutex> lock(latest_samples_mutex_);
                latest_samples_in_window_[received_data.mac_address] = received_data;
                spdlog::get("RestApiConsumer")->debug("[Loop] Updated latest sample for MAC: {} (Name: {}, Temp: {}, Hum: {}, RSSI: {})",
                                                      received_data.mac_address, received_data.predefined_name,
                                                      received_data.temperature, received_data.humidity, (int)received_data.rssi);

            } else {
                spdlog::get("RestApiConsumer")->trace("[Loop] queue.pop timed out or queue was empty.");
            }

            now = std::chrono::steady_clock::now();
            spdlog::get("RestApiConsumer")->trace("[Loop] Checking window expiration. Current time - Window start time = {}s. Duration: {}s.",
                                                  std::chrono::duration_cast<std::chrono::seconds>(now - window_start_time_).count(),
                                                  logging_window_duration_.count());

            if (now - window_start_time_ >= logging_window_duration_) {
                std::lock_guard<std::mutex> lock(latest_samples_mutex_);
                spdlog::get("RestApiConsumer")->info("[Loop] Window expired. latest_samples_in_window_ size: {}.", latest_samples_in_window_.size());

                if (!latest_samples_in_window_.empty()) {
                    std::string timestamp_str = formatTimestamp(std::chrono::system_clock::now());
                    std::vector<char> binary_data = SensorDataSerializer::serializeSensorDataMap(latest_samples_in_window_);

                    spdlog::get("RestApiConsumer")->info("[Loop] Posting aggregated sample (count: {}) for timestamp {} to REST API. Blob size: {} bytes.",
                                                          latest_samples_in_window_.size(), timestamp_str, binary_data.size());

                    // --- Perform HTTP POST using libcurl ---
                    CURL *curl = curl_easy_init(); // Get a curl handle
                    std::string readBuffer; // To store response from the server

                    if (curl) {
                        spdlog::get("RestApiConsumer")->trace("[Loop] cURL handle initialized.");
                        curl_easy_setopt(curl, CURLOPT_URL, api_url_.c_str());
                        curl_easy_setopt(curl, CURLOPT_POST, 1L); // Set as POST request
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, binary_data.data());
                        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, binary_data.size());
                        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L); // 5 second timeout

                        struct curl_slist *headers = NULL;
                        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
                        spdlog::get("RestApiConsumer")->trace("[Loop] Setting Content-Type header.");
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                        spdlog::get("RestApiConsumer")->trace("[Loop] Performing cURL POST...");
                        CURLcode res = curl_easy_perform(curl); // Perform the request
                        if (res != CURLE_OK) {
                            spdlog::get("RestApiConsumer")->error("[Loop] curl_easy_perform() failed: {}", curl_easy_strerror(res));
                        } else {
                            long http_code = 0;
                            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                            spdlog::get("RestApiConsumer")->info("[Loop] Successfully posted aggregated data. HTTP Status: {}, Response: {}", http_code, readBuffer);
                        }

                        curl_easy_cleanup(curl); // Clean up the curl handle
                        curl_slist_free_all(headers); // Free the header list
                        spdlog::get("RestApiConsumer")->trace("[Loop] cURL handle cleaned up.");
                    } else {
                        spdlog::get("RestApiConsumer")->error("[Loop] Failed to initialize curl.");
                    }

                    latest_samples_in_window_.clear(); // Clear the map for the next window
                    spdlog::get("RestApiConsumer")->trace("[Loop] latest_samples_in_window_ cleared for new window.");
                } else {
                    spdlog::get("RestApiConsumer")->info("[Loop] Window expired, but no samples received in this window. Not posting.");
                }
                window_start_time_ = now; // Start a new window
                spdlog::get("RestApiConsumer")->info("[Loop] New logging window started.");
            }
        }
        spdlog::get("RestApiConsumer")->info("[Loop] Exited.");
    } catch (const std::exception& e) {
        spdlog::get("RestApiConsumer")->critical("[Loop] FATAL ERROR: Unhandled exception: {}", e.what());
    } catch (...) {
        spdlog::get("RestApiConsumer")->critical("[Loop] FATAL ERROR: Unknown unhandled exception.");
    }
}

/**
 * @brief Helper function to format a std::chrono::system_clock::time_point to a string.
 * @param tp The time_point to format.
 * @return A formatted string (e.g., "YYYY-MM-DDTHH:MM:SSZ").
 */
std::string RestApiConsumer::formatTimestamp(std::chrono::system_clock::time_point tp) {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_utc;
    #ifdef _WIN32
        gmtime_s(&tm_utc, &tt);
    #else
        gmtime_r(&tt, &tm_utc);
    #endif
    std::stringstream ss;
    ss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}
