// SQLiteDatabaseManager.h
#ifndef SQLITE_DATABASE_MANAGER_H
#define SQLITE_DATABASE_MANAGER_H

#include "IDatabaseManager.h" // Inherits from IDatabaseManager
#include <sqlite3.h>          // SQLite C API
#include <string>
#include <vector> // For binary data

// spdlog include
#include "spdlog/spdlog.h"

/**
 * @brief Concrete implementation of IDatabaseManager for SQLite.
 */
class SQLiteDatabaseManager : public IDatabaseManager {
public:
    /**
     * @brief Constructs a SQLiteDatabaseManager.
     */
    SQLiteDatabaseManager();

    /**
     * @brief Destroys the SQLiteDatabaseManager, ensuring the database connection is closed.
     */
    ~SQLiteDatabaseManager();

    /**
     * @brief Initializes the SQLite database connection and creates the necessary table.
     * @param db_path The path to the SQLite database file.
     * @return True on success, false on failure.
     */
    bool initialize(const std::string& db_path) override;

    /**
     * @brief Inserts a single SensorData object into the database.
     * @param data The SensorData object to insert.
     * @return True on successful insertion, false on failure.
     * @deprecated This method is being replaced by insertAggregatedSensorData for windowed logging.
     */
    bool insertSensorData(const SensorData& data) override;

    /**
     * @brief Inserts aggregated sensor data (binary blob) into the database.
     * This method is intended for saving data from multiple sensors within a single time window.
     * @param timestamp_str The timestamp string for the aggregated data.
     * @param binary_data A vector of characters representing the serialized binary data.
     * @return True on successful insertion, false on failure.
     */
    bool insertAggregatedSensorData(const std::string& timestamp_str, const std::vector<char>& binary_data) override;

    /**
     * @brief Shuts down the database connection.
     */
    void shutdown() override;

private:
    sqlite3* db_; ///< Pointer to the SQLite database connection handle
};

#endif // SQLITE_DATABASE_MANAGER_H
