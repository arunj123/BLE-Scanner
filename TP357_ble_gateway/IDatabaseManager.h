// IDatabaseManager.h
#ifndef IDATABASE_MANAGER_H
#define IDATABASE_MANAGER_H

#include "SensorData.h" // Include SensorData definition
#include <string>
#include <vector> // For binary data

/**
 * @brief Abstract base class for database management.
 * Defines the interface for interacting with a database.
 */
class IDatabaseManager {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~IDatabaseManager() = default;

    /**
     * @brief Initializes the database connection and schema.
     * @param db_path The path to the database file.
     * @return True on success, false on failure.
     */
    virtual bool initialize(const std::string& db_path) = 0;

    /**
     * @brief Inserts a single SensorData object into the database.
     * @param data The SensorData object to insert.
     * @return True on successful insertion, false on failure.
     * @deprecated This method is being replaced by insertAggregatedSensorData for windowed logging.
     */
    virtual bool insertSensorData(const SensorData& data) = 0;

    /**
     * @brief Inserts aggregated sensor data (binary blob) into the database.
     * This method is intended for saving data from multiple sensors within a single time window.
     * @param timestamp_str The timestamp string for the aggregated data.
     * @param binary_data A vector of characters representing the serialized binary data.
     * @return True on successful insertion, false on failure.
     */
    virtual bool insertAggregatedSensorData(const std::string& timestamp_str, const std::vector<char>& binary_data) = 0;

    /**
     * @brief Shuts down the database connection.
     */
    virtual void shutdown() = 0;
};

#endif // IDATABASE_MANAGER_H
