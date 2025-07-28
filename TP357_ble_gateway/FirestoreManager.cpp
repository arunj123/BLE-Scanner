// FirestoreManager.cpp
#include "FirestoreManager.h"
#include <iostream>
#include <chrono>
#include <random> // For simulating network flakiness
#include <future> // For std::future to wait on Firebase async operations
#include <thread> // Required for std::this_thread::sleep_for

// Firebase Admin SDK logging (optional)
// #include "firebase/log.h"

FirestoreManager::FirestoreManager()
    : app_(nullptr), db_(nullptr),
      simulated_online_status_(false), // Default to false, set to true on successful init
      is_initialized_(false) { // Initialize atomic bool
}

FirestoreManager::~FirestoreManager() {
    shutdown();
}

/**
 * @brief Initializes the Firebase App and Firestore client.
 * @param config_path Path to the Firebase service account key JSON file.
 * @return True on success, false on failure.
 */
bool FirestoreManager::initialize(const std::string& config_path) {
    if (is_initialized_.load()) { // Use .load() for atomic bool
        std::cout << "FirestoreManager already initialized." << std::endl;
        simulated_online_status_.store(true); // Ensure online if already initialized
        return true;
    }

    std::cout << "Initializing Firebase App with service account: " << config_path << std::endl;

    // Set Firebase logging (optional, for debugging)
    // firebase::SetLogLevel(firebase::kLogLevelInfo);

    // Create the Firebase App using the service account path directly
    // This is the correct way for SDK v13.0.0+
    app_ = firebase::App::Create(firebase::AppOptions(), config_path.c_str());
    if (!app_) {
        std::cerr << "Failed to create Firebase App. Check service account path and permissions." << std::endl;
        return false;
    }

    // Get the Firestore instance
    db_ = firebase::firestore::Firestore::GetInstance(app_);
    if (!db_) {
        std::cerr << "Failed to get Firestore instance." << std::endl;
        delete app_; // Corrected: Use delete on the pointer
        app_ = nullptr;
        return false;
    }

    is_initialized_.store(true); // Use .store() for atomic bool
    simulated_online_status_.store(true); // Assume online after successful initialization
    std::cout << "Firebase Firestore initialized successfully." << std::endl;
    return true;
}

/**
 * @brief Inserts a SensorData object into Firestore.
 * @param data The SensorData object to insert.
 * @return True on successful insertion, false on failure.
 */
bool FirestoreManager::insertSensorData(const SensorData& data) {
    if (!is_initialized_.load() || !simulated_online_status_.load()) { // Use .load()
        std::cerr << "Firestore is offline or not initialized. Cannot insert data." << std::endl;
        return false;
    }

    // Prepare data for Firestore document using firebase::firestore::MapFieldValue
    firebase::firestore::MapFieldValue doc_data;
    doc_data["predefined_name"] = firebase::firestore::FieldValue::String(data.predefined_name);
    doc_data["temperature"] = firebase::firestore::FieldValue::Double(data.temperature);
    doc_data["humidity"] = firebase::firestore::FieldValue::Double(data.humidity);
    doc_data["rssi"] = firebase::firestore::FieldValue::Integer(data.rssi);

    // Convert std::chrono::system_clock::time_point to Firebase Timestamp
    // Firebase Timestamp uses microseconds since epoch
    auto epoch = data.timestamp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(epoch);
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch - seconds);

    // Corrected: firebase::Timestamp directly, not firebase::firestore::Timestamp
    firebase::Timestamp firebase_timestamp(seconds.count(), nanoseconds.count());
    doc_data["timestamp"] = firebase::firestore::FieldValue::Timestamp(firebase_timestamp);

    // Generate a document ID (e.g., based on predefined name and timestamp for uniqueness)
    std::string doc_id = data.predefined_name + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(data.timestamp.time_since_epoch()).count());

    // Add data to a new document in the "sensor_readings" collection
    firebase::Future<void> future = db_->Collection("sensor_readings").Document(doc_id).Set(doc_data);

    // Wait for the operation to complete using a loop (more robust if .Wait() is problematic)
    // In a real application, you might use an asynchronous callback or a more sophisticated
    // blocking mechanism depending on your threading model.
    while (future.status() == firebase::kFutureStatusPending) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay to avoid busy-waiting
    }

    // Corrected: Use firebase::firestore::kErrorNone for success check
    if (future.error() == firebase::firestore::kErrorNone) {
        // std::cout << "Data successfully written to Firestore for " << data.predefined_name << std::endl;
        return true;
    } else {
        std::cerr << "Error writing to Firestore for " << data.predefined_name << ": "
                  << future.error_message() << " (Error Code: " << future.error() << ")" << std::endl;
        return false;
    }
}

/**
 * @brief Checks if the Firestore manager is initialized and conceptually "online".
 * This is a simple check for initialization; real network connectivity
 * would require more sophisticated checks.
 * @return True if initialized and considered online, false otherwise.
 */
bool FirestoreManager::isOnline() const {
    // In a real application, you might add network reachability checks here.
    // For now, it simply reflects if the SDK was successfully initialized.
    return is_initialized_.load() && simulated_online_status_.load(); // Use .load()
}

/**
 * @brief Shuts down the Firebase App and Firestore client.
 */
void FirestoreManager::shutdown() {
    if (db_) {
        // Firestore instance is managed by the App, no explicit delete needed for db_
        db_ = nullptr; // Just clear the pointer
    }
    if (app_) {
        std::cout << "Shutting down Firebase App." << std::endl;
        delete app_; // Corrected: Use delete on the pointer
        app_ = nullptr;
    }
    is_initialized_.store(false); // Use .store()
    simulated_online_status_.store(false);
}

/**
 * @brief Sets the simulated online status.
 * @param online True to set online, false to set offline.
 */
void FirestoreManager::setSimulatedOnlineStatus(bool online) {
    simulated_online_status_.store(online);
    std::cout << "Firestore simulated status set to: " << (online ? "ONLINE" : "OFFLINE") << std::endl;
}
