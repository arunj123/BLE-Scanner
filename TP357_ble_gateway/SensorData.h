// SensorData.h
#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <string>
#include <chrono> // For std::chrono::system_clock::time_point
#include <cstdint> // For int8_t

/**
 * @brief Structure to hold parsed sensor data.
 */
struct SensorData {
    std::string mac_address;        ///< MAC address of the device
    std::string predefined_name;    ///< User-defined name for the device (e.g., "Living Room Sensor")
    std::string decoded_device_name;///< Device name decoded from advertising data (e.g., "TP357 (E4F0)")
    double temperature;             ///< Temperature reading in Celsius
    double humidity;                ///< Humidity reading in percentage
    int8_t rssi;                    ///< RSSI (Received Signal Strength Indicator)
    std::chrono::system_clock::time_point timestamp; ///< Timestamp of when the data was received

    // Default constructor for use with std::optional and dummy queue signals
    SensorData() : temperature(0.0), humidity(0.0), rssi(0), timestamp(std::chrono::system_clock::now()) {}

    /**
     * @brief Constructs a SensorData object with provided values.
     */
    SensorData(const std::string& mac, const std::string& p_name, const std::string& d_name,
               double temp, double hum, int8_t r, std::chrono::system_clock::time_point ts)
        : mac_address(mac), predefined_name(p_name), decoded_device_name(d_name),
          temperature(temp), humidity(hum), rssi(r), timestamp(ts) {}
};

#endif // SENSOR_DATA_H
