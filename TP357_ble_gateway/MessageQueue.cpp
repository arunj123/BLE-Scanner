// MessageQueue.cpp
#include "MessageQueue.h"
#include <iostream>

/**
 * @brief Pushes a SensorData object onto the queue.
 * @param data The SensorData object to push.
 */
void MessageQueue::push(const SensorData& data) {
    std::lock_guard<std::mutex> lock(mutex_); // Acquire lock
    queue_.push(data);                        // Push data
    cv_.notify_one();                         // Notify one waiting consumer thread
}

/**
 * @brief Pops a SensorData object from the queue.
 * This method will block until data is available in the queue.
 * @return An optional containing the SensorData object if available, std::nullopt otherwise.
 */
std::optional<SensorData> MessageQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_); // Acquire unique lock
    // Wait until the queue is not empty. This releases the lock while waiting.
    cv_.wait(lock, [this]{ return !queue_.empty(); });

    // After waking up, the lock is re-acquired.
    if (queue_.empty()) {
        return std::nullopt;
    }

    SensorData data = queue_.front(); // Get data from the front
    queue_.pop();                     // Remove data from the front
    return data;                      // Return the data
}

/**
 * @brief Pops a SensorData object from the queue with a timeout.
 * This method will block for up to the specified timeout duration or until data is available.
 * @param timeout The maximum time to wait for data.
 * @return An optional containing the SensorData object if available, std::nullopt if timeout occurs or queue is empty.
 */
std::optional<SensorData> MessageQueue::pop(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_); // Acquire unique lock
    // Wait until the queue is not empty or timeout occurs.
    bool data_available = cv_.wait_for(lock, timeout, [this]{ return !queue_.empty(); });

    if (data_available && !queue_.empty()) {
        SensorData data = queue_.front(); // Get data from the front
        queue_.pop();                     // Remove data from the front
        return data;                      // Return the data
    }
    return std::nullopt; // Timeout occurred or queue was empty
}

/**
 * @brief Checks if the queue is empty.
 * @return True if the queue is empty, false otherwise.
 */
bool MessageQueue::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_); // Acquire lock for const access
    return queue_.empty();
}
