// SQLiteDatabaseManager.cpp
#include "SQLiteDatabaseManager.h"
#include <iostream>
#include <cstdio> // For remove()

SQLiteDatabaseManager::SQLiteDatabaseManager() : db_(nullptr) {
    // Constructor: db_ is initialized to nullptr.
}

SQLiteDatabaseManager::~SQLiteDatabaseManager() {
    shutdown(); // Ensure database is closed on destruction.
}

/**
 * @brief Initializes the SQLite database connection and creates the necessary table.
 * This version creates a table for aggregated binary sensor data.
 * @param db_path The path to the SQLite database file.
 * @return True on success, false on failure.
 */
bool SQLiteDatabaseManager::initialize(const std::string& db_path) {
    // Open the database connection
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr; // Ensure db_ is nullptr if open fails
        return false;
    }
    std::cout << "Opened database successfully: " << db_path << std::endl;

    // SQL statement to create the aggregated sensor_readings table
    // It will store a timestamp and a BLOB (binary large object) of aggregated data.
    const char* sql_create_table =
        "CREATE TABLE IF NOT EXISTS sensor_readings_aggregated ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "TIMESTAMP TEXT NOT NULL,"
        "DATA BLOB"
        ");";

    char* err_msg = nullptr;
    rc = sqlite3_exec(db_, sql_create_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error creating table: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        sqlite3_close(db_);
        db_ = nullptr; // Ensure db_ is nullptr if table creation fails
        return false;
    }
    std::cout << "Table 'sensor_readings_aggregated' created successfully or already exists." << std::endl;

    return true;
}

/**
 * @brief Inserts a single SensorData object into the database.
 * This method is deprecated and no longer used by DataProcessor for windowed logging.
 * @param data The SensorData object to insert.
 * @return True on successful insertion, false on failure.
 */
bool SQLiteDatabaseManager::insertSensorData(const SensorData& data) {
    // This method is deprecated for the new logging strategy.
    // It will still work if called directly, but DataProcessor now calls insertAggregatedSensorData.
    std::cerr << "Warning: insertSensorData is deprecated and should not be called for windowed logging." << std::endl;
    return false; // Indicate failure as it's not the intended path
}

/**
 * @brief Inserts aggregated sensor data (binary blob) into the database.
 * This method is intended for saving data from multiple sensors within a single time window.
 * @param timestamp_str The timestamp string for the aggregated data.
 * @param binary_data A vector of characters representing the serialized binary data.
 * @return True on successful insertion, false on failure.
 */
bool SQLiteDatabaseManager::insertAggregatedSensorData(const std::string& timestamp_str, const std::vector<char>& binary_data) {
    if (!db_) {
        std::cerr << "Database not open. Cannot insert aggregated data." << std::endl;
        return false;
    }

    const char* sql_insert =
        "INSERT INTO sensor_readings_aggregated (TIMESTAMP, DATA) VALUES (?, ?);";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql_insert, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement for aggregated data: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    // Bind timestamp
    sqlite3_bind_text(stmt, 1, timestamp_str.c_str(), -1, SQLITE_TRANSIENT);

    // Bind binary data (BLOB)
    // Use SQLITE_STATIC to indicate that the data is owned by the caller and will not change
    // during the lifetime of the statement. The vector's data() pointer is valid until the vector is destroyed.
    sqlite3_bind_blob(stmt, 2, binary_data.data(), static_cast<int>(binary_data.size()), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to execute statement for aggregated data: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return false;
    }

    sqlite3_finalize(stmt);
    std::cout << "Successfully inserted aggregated data into SQLite for timestamp: " << timestamp_str << std::endl;
    return true;
}

/**
 * @brief Shuts down the database connection.
 */
void SQLiteDatabaseManager::shutdown() {
    if (db_) {
        int rc = sqlite3_close(db_);
        if (rc != SQLITE_OK) {
            std::cerr << "Error closing database: " << sqlite3_errmsg(db_) << std::endl;
        } else {
            std::cout << "Database closed." << std::endl;
        }
        db_ = nullptr;
    }
}
