// SensorDataSerializer.cpp
#include "SensorDataSerializer.h"
#include <cstdio>  // For sscanf
#include <array>   // For std::array

// spdlog include
#include "spdlog/spdlog.h"

/**
 * @brief Serializes a map of SensorData objects into a binary vector.
 * @param data_map The map containing SensorData to serialize.
 * @return A vector of characters representing the binary data.
 */
std::vector<char> SensorDataSerializer::serializeSensorDataMap(const std::map<std::string, SensorData>& data_map) {
    std::vector<char> buffer;
    // Reserve some space to reduce reallocations (estimation)
    // Roughly: 1 byte for count + N * (6 bytes MAC + 8 bytes temp + 8 bytes hum + 1 byte RSSI)
    buffer.reserve(1 + data_map.size() * (6 + sizeof(double) + sizeof(double) + 1));

    // Write total number of sensors in this aggregation (1 byte)
    uint8_t num_sensors = static_cast<uint8_t>(data_map.size());
    buffer.push_back(num_sensors);
    spdlog::get("SensorDataSerializer")->debug("Serializing {} sensors into binary blob.", num_sensors);

    for (const auto& pair : data_map) {
        const SensorData& data = pair.second;

        // MAC Address (fixed 6 bytes)
        std::array<uint8_t, 6> mac_bytes;
        // Parse MAC string "AA:BB:CC:DD:EE:FF" into 6 bytes
        if (std::sscanf(data.mac_address.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
                        &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]) == 6) {
            for (size_t i = 0; i < 6; ++i) {
                buffer.push_back(static_cast<char>(mac_bytes[i]));
            }
            spdlog::get("SensorDataSerializer")->debug("  - Serialized MAC: {}", data.mac_address);
        } else {
            spdlog::get("SensorDataSerializer")->error("Failed to parse MAC address for serialization: {}", data.mac_address);
            // Push 6 zero bytes as fallback for malformed MAC
            for (size_t i = 0; i < 6; ++i) {
                buffer.push_back(0);
            }
        }

        // Temperature (double)
        const char* temp_bytes = reinterpret_cast<const char*>(&data.temperature);
        for (size_t i = 0; i < sizeof(double); ++i) buffer.push_back(temp_bytes[i]);
        spdlog::get("SensorDataSerializer")->debug("  - Serialized Temperature: {}", data.temperature);

        // Humidity (double)
        const char* hum_bytes = reinterpret_cast<const char*>(&data.humidity);
        for (size_t i = 0; i < sizeof(double); ++i) buffer.push_back(hum_bytes[i]);
        spdlog::get("SensorDataSerializer")->debug("  - Serialized Humidity: {}", data.humidity);

        // RSSI (int8_t)
        buffer.push_back(static_cast<char>(data.rssi));
        spdlog::get("SensorDataSerializer")->debug("  - Serialized RSSI: {}", (int)data.rssi);
    }
    spdlog::get("SensorDataSerializer")->debug("Serialization complete. Blob size: {} bytes.", buffer.size());
    return buffer;
}
