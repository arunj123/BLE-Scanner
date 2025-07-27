// SQLiteDatabaseManager.cpp
#include "SQLiteDatabaseManager.h"
#include <iostream>
#include <ctime>     // For std::time_t and std::gmtime
#include <iomanip>   // For std::put_time
#include <sstream>   // For std::stringstream

SQLiteDatabaseManager::SQLiteDatabaseManager() : db_(nullptr), is_initialized_(false) {}

SQLiteDatabaseManager::~SQLiteDatabaseManager() {
    shutdown(); // Ensure database is closed on destruction
}

/**
 * @brief Initializes the SQLite database. Opens the database file and creates the table if it doesn't exist.
 * @param db_path The file path for the SQLite database.
 * @return True on success, false on failure.
 */
bool SQLiteDatabaseManager::initialize(const std::string& db_path) {
    if (is_initialized_) {
        std::cerr << "Database already initialized." << std::endl;
        return false;
    }

    // Open the database connection
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    } else {
        std::cout << "Opened database successfully: " << db_path << std::endl;
    }

    // SQL statement to create the table if it does not exist
    const char* sql = "CREATE TABLE IF NOT EXISTS sensor_readings ("
                      "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                      "MAC_ADDRESS TEXT NOT NULL,"
                      "PREDEFINED_NAME TEXT,"
                      "DECODED_DEVICE_NAME TEXT,"
                      "TEMPERATURE REAL,"
                      "HUMIDITY REAL,"
                      "RSSI INTEGER,"
                      "TIMESTAMP TEXT NOT NULL);";

    char *zErrMsg = 0;
    rc = sqlite3_exec(db_, sql, 0, 0, &zErrMsg); // Execute the SQL statement
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error creating table: " << zErrMsg << std::endl;
        sqlite3_free(zErrMsg); // Free the error message memory
        sqlite3_close(db_);    // Close the database on error
        db_ = nullptr;
        return false;
    } else {
        std::cout << "Table 'sensor_readings' created successfully or already exists." << std::endl;
    }

    is_initialized_ = true;
    return true;
}

/**
 * @brief Inserts a SensorData object into the SQLite database.
 * @param data The SensorData object to insert.
 * @return True on successful insertion, false on failure.
 */
bool SQLiteDatabaseManager::insertSensorData(const SensorData& data) {
    if (!is_initialized_ || !db_) {
        std::cerr << "Database not initialized or connection is null." << std::endl;
        return false;
    }

    sqlite3_stmt *stmt; // Prepared statement object
    int rc;

    // Convert std::chrono::system_clock::time_point to a human-readable string (ISO 8601 format)
    std::time_t tt = std::chrono::system_clock::to_time_t(data.timestamp);
    std::tm tm_buf;
    // Use platform-specific safe version of gmtime
    #ifdef _WIN32
    gmtime_s(&tm_buf, &tt); // For Windows
    #else
    gmtime_r(&tt, &tm_buf); // For POSIX systems (Linux, macOS)
    #endif
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ"); // Format as YYYY-MM-DDTHH:MM:SSZ

    // SQL INSERT statement with placeholders
    std::string sql = "INSERT INTO sensor_readings (MAC_ADDRESS, PREDEFINED_NAME, DECODED_DEVICE_NAME, TEMPERATURE, HUMIDITY, RSSI, TIMESTAMP) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?);";

    // Prepare the SQL statement
    rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }

    // Bind values to the placeholders
    sqlite3_bind_text(stmt, 1, data.mac_address.c_str(), -1, SQLITE_TRANSIENT);
    // If predefined_name is empty, bind NULL to the database column
    sqlite3_bind_text(stmt, 2, data.predefined_name.empty() ? nullptr : data.predefined_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, data.decoded_device_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, data.temperature);
    sqlite3_bind_double(stmt, 5, data.humidity);
    sqlite3_bind_int(stmt, 6, data.rssi);
    sqlite3_bind_text(stmt, 7, ss.str().c_str(), -1, SQLITE_TRANSIENT);

    // Execute the prepared statement
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) { // SQLITE_DONE indicates successful completion for INSERT, UPDATE, DELETE
        std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt); // Finalize the statement on error
        return false;
    }

    sqlite3_finalize(stmt); // Finalize the statement
    return true;
}

/**
 * @brief Shuts down the SQLite database connection.
 */
void SQLiteDatabaseManager::shutdown() {
    if (db_) {
        sqlite3_close(db_); // Close the database connection
        db_ = nullptr;
        is_initialized_ = false;
        std::cout << "Database closed." << std::endl;
    }
}
