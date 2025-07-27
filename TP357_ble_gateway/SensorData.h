// SensorData.h
#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <string>
#include <chrono> // For std::chrono::system_clock::time_point

/**
 * @brief Structure to hold parsed sensor data along with a timestamp.
 */
struct SensorData {
    std::string mac_address;              ///< MAC address of the Bluetooth device
    std::string predefined_name;          ///< Custom predefined name for the device
    std::string decoded_device_name;      ///< Device name decoded from advertising data
    double temperature;                   ///< Temperature reading
    double humidity;                      ///< Humidity reading
    int8_t rssi;                         ///< Received Signal Strength Indicator
    std::chrono::system_clock::time_point timestamp; ///< Timestamp of when the packet was received

    /**
     * @brief Constructor for SensorData.
     * @param mac MAC address.
     * @param pre_name Predefined name.
     * @param dec_name Decoded device name.
     * @param temp Temperature.
     * @param hum Humidity.
     * @param r RSSI.
     * @param ts Timestamp.
     */
    SensorData(const std::string& mac, const std::string& pre_name, const std::string& dec_name,
               double temp, double hum, int8_t r, std::chrono::system_clock::time_point ts)
        : mac_address(mac), predefined_name(pre_name), decoded_device_name(dec_name),
          temperature(temp), humidity(hum), rssi(r), timestamp(ts) {}

    // Default constructor needed for std::optional and queue operations
    SensorData() : temperature(-999.0), humidity(-999.0), rssi(0) {}
};

#endif // SENSOR_DATA_H
