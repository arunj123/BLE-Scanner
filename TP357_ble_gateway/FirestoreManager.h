// FirestoreManager.h
#ifndef FIRESTORE_MANAGER_H
#define FIRESTORE_MANAGER_H

#include "IFirestoreManager.h"
#include <string>
#include <atomic>
#include <unordered_map> // Required for firebase::firestore::MapFieldValue

// Include Firebase Admin SDK headers
#include "firebase/app.h"           // firebase::App and firebase::AppOptions are defined here
#include "firebase/firestore.h"
#include "firebase/firestore/timestamp.h" // Explicitly include for firebase::Timestamp
#include "firebase/future.h"        // For firebase::Future (firebase::kErrorNone is often here)


/**
 * @brief Concrete implementation of IFirestoreManager for Firebase Firestore.
 * This class integrates with the Firebase Admin SDK for C++.
 */
class FirestoreManager : public IFirestoreManager {
public:
    /**
     * @brief Constructs a FirestoreManager.
     */
    FirestoreManager();

    /**
     * @brief Destroys the FirestoreManager, ensuring Firebase resources are cleaned up.
     */
    ~FirestoreManager();

    /**
     * @brief Initializes the Firebase App and Firestore client.
     * @param config_path Path to the Firebase service account key JSON file.
     * @return True on success, false on failure.
     */
    bool initialize(const std::string& config_path) override;

    /**
     * @brief Inserts a SensorData object into Firestore.
     * @param data The SensorData object to insert.
     * @return True on successful insertion, false on failure.
     */
    bool insertSensorData(const SensorData& data) override;

    /**
     * @brief Checks if the Firestore manager is initialized and conceptually "online".
     * This is a simple check for initialization; real network connectivity
     * would require more sophisticated checks.
     * @return True if initialized and considered online, false otherwise.
     */
    bool isOnline() const override;

    /**
     * @brief Shuts down the Firebase App and Firestore client.
     */
    void shutdown() override;

    /**
     * @brief Sets the simulated online status.
     * This is primarily for testing fallback logic when a real network check isn't feasible.
     * @param online True to set online, false to set offline.
     */
    void setSimulatedOnlineStatus(bool online);

private:
    firebase::App* app_;             ///< Firebase App instance
    firebase::firestore::Firestore* db_;  ///< Firestore client instance
    std::atomic<bool> simulated_online_status_; ///< Flag to simulate online/offline state
    std::atomic<bool> is_initialized_;    ///< Flag to track successful Firebase initialization (made atomic)
};

#endif // FIRESTORE_MANAGER_H
