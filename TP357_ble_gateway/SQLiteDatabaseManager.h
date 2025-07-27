// SQLiteDatabaseManager.h
#ifndef SQLITE_DATABASE_MANAGER_H
#define SQLITE_DATABASE_MANAGER_H

#include "IDatabaseManager.h" // SQLiteDatabaseManager implements IDatabaseManager
#include <sqlite3.h> // SQLite C API header
#include <string>

/**
 * @brief Concrete implementation of IDatabaseManager for SQLite3.
 * Handles opening, creating tables, and inserting data into an SQLite database.
 */
class SQLiteDatabaseManager : public IDatabaseManager {
public:
    /**
     * @brief Constructs an SQLiteDatabaseManager.
     */
    SQLiteDatabaseManager();

    /**
     * @brief Destroys the SQLiteDatabaseManager, ensuring the database is closed.
     */
    ~SQLiteDatabaseManager();

    /**
     * @brief Initializes the SQLite database. Opens the database file and creates the table if it doesn't exist.
     * @param db_path The file path for the SQLite database.
     * @return True on success, false on failure.
     */
    bool initialize(const std::string& db_path) override;

    /**
     * @brief Inserts a SensorData object into the SQLite database.
     * @param data The SensorData object to insert.
     * @return True on successful insertion, false on failure.
     */
    bool insertSensorData(const SensorData& data) override;

    /**
     * @brief Shuts down the SQLite database connection.
     */
    void shutdown() override;

private:
    sqlite3 *db_;           ///< SQLite database connection handle
    bool is_initialized_;   ///< Flag to track initialization status
};

#endif // SQLITE_DATABASE_MANAGER_H
