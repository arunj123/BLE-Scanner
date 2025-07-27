// IFirestoreManager.h
#ifndef IFIRESTORE_MANAGER_H
#define IFIRESTORE_MANAGER_H

#include "SensorData.h"
#include <string>

/**
 * @brief Abstract base class (interface) for Firestore managers.
 * Defines the contract for interacting with a Firestore database.
 */
class IFirestoreManager {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~IFirestoreManager() = default;

    /**
     * @brief Initializes the Firestore connection.
     * @param config_path Path to the Firebase service account key JSON file or other configuration.
     * @return True on success, false on failure.
     */
    virtual bool initialize(const std::string& config_path) = 0;

    /**
     * @brief Inserts a SensorData object into Firestore.
     * @param data The SensorData object to insert.
     * @return True on successful insertion, false on failure.
     */
    virtual bool insertSensorData(const SensorData& data) = 0;

    /**
     * @brief Checks if the Firestore connection is currently considered "online" or reachable.
     * This is a conceptual check for fallback logic.
     * @return True if online, false otherwise.
     */
    virtual bool isOnline() const = 0;

    /**
     * @brief Shuts down the Firestore connection and performs any necessary cleanup.
     */
    virtual void shutdown() = 0;
};

#endif // IFIRESTORE_MANAGER_H
