// SensorDataSerializer.h
#ifndef SENSOR_DATA_SERIALIZER_H
#define SENSOR_DATA_SERIALIZER_H

#include "SensorData.h"
#include <vector>
#include <map>
#include <string>
#include <cstdint> // For uint8_t

// spdlog include
#include "spdlog/spdlog.h"

/**
 * @brief Utility class for serializing and deserializing SensorData.
 * This class provides static methods for converting a map of SensorData
 * into a compact binary format (BLOB) and vice-versa.
 */
class SensorDataSerializer {
public:
    /**
     * @brief Serializes a map of SensorData objects into a binary vector.
     * The binary format is designed for compactness and includes data for each sensor.
     *
     * Binary Format:
     * 1. Total Number of Sensors (1 byte: uint8_t)
     * 2. For each SensorData entry:
     * - MAC Address (6 bytes: raw bytes) - Fixed length
     * - Temperature (8 bytes: double)
     * - Humidity (8 bytes: double)
     * - RSSI (1 byte: int8_t)
     *
     * @param data_map The map containing SensorData to serialize (keyed by MAC address).
     * @return A vector of characters representing the binary data.
     */
    static std::vector<char> serializeSensorDataMap(const std::map<std::string, SensorData>& data_map);

    // Note: A deserialize method could be added here if needed for C++-side deserialization.
};

#endif // SENSOR_DATA_SERIALIZER_H
