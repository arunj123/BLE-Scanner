// MessageQueue.h
#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include "SensorData.h" // MessageQueue directly uses SensorData
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional> // C++17 for std::optional

/**
 * @brief A thread-safe message queue for SensorData objects.
 * This queue allows multiple threads to safely push and pop data.
 */
class MessageQueue {
public:
    /**
     * @brief Pushes a SensorData object onto the queue.
     * @param data The SensorData object to push.
     */
    void push(const SensorData& data);

    /**
     * @brief Pops a SensorData object from the queue.
     * This method will block until data is available in the queue.
     * @return An optional containing the SensorData object if available, std::nullopt otherwise.
     */
    std::optional<SensorData> pop();

    /**
     * @brief Checks if the queue is empty.
     * @return True if the queue is empty, false otherwise.
     */
    bool isEmpty() const;

private:
    std::queue<SensorData> queue_;              ///< The underlying queue
    mutable std::mutex mutex_;                  ///< Mutex to protect access to the queue
    std::condition_variable cv_;                ///< Condition variable to signal when data is available
};

#endif // MESSAGE_QUEUE_H
