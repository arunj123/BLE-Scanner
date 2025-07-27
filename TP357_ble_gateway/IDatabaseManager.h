// IDatabaseManager.h
#ifndef IDATABASE_MANAGER_H
#define IDATABASE_MANAGER_H

#include "SensorData.h" // IDatabaseManager directly uses SensorData
#include <string>

/**
 * @brief Abstract base class (interface) for database managers.
 * This interface defines the contract for any database backend
 * that wants to store SensorData.
 */
class IDatabaseManager {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup of derived classes.
     */
    virtual ~IDatabaseManager() = default;

    /**
     * @brief Initializes the database connection and schema.
     * @param db_path The path or connection string for the database.
     * @return True on success, false on failure.
     */
    virtual bool initialize(const std::string& db_path) = 0;

    /**
     * @brief Inserts a SensorData object into the database.
     * @param data The SensorData object to insert.
     * @return True on successful insertion, false on failure.
     */
    virtual bool insertSensorData(const SensorData& data) = 0;

    /**
     * @brief Shuts down the database connection and performs any necessary cleanup.
     */
    virtual void shutdown() = 0;
};

#endif // IDATABASE_MANAGER_H
