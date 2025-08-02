// IDataConsumer.h
#ifndef I_DATA_CONSUMER_H
#define I_DATA_CONSUMER_H

#include "MessageQueue.h" // Consumers need access to the MessageQueue
#include <memory>         // For std::unique_ptr

/**
 * @brief Abstract base class (interface) for data consumers.
 * Any class that processes SensorData from the MessageQueue should implement this interface.
 */
class IDataConsumer {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~IDataConsumer() = default;

    /**
     * @brief Starts the data consumption process.
     * This method is typically implemented to run in a separate thread.
     */
    virtual void startConsuming() = 0;

    /**
     * @brief Signals the data consumption process to stop and waits for its completion.
     */
    virtual void stopConsuming() = 0;
};

#endif // I_DATA_CONSUMER_H
