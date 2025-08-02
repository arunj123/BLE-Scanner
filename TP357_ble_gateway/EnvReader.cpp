// EnvReader.cpp
#include "EnvReader.h"
#include <fstream>
#include <string>
#include <algorithm> // For std::remove
#include <spdlog/spdlog.h> // Include spdlog for logging

/**
 * @brief Loads environment variables from the specified .env file.
 * @param file_path The path to the .env file.
 * @return True if the file was loaded successfully, false otherwise.
 */
bool EnvReader::load(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        spdlog::get("EnvReader")->warn("Could not open .env file: {}", file_path);
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines or comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t equals_pos = line.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = line.substr(0, equals_pos);
            std::string value = line.substr(equals_pos + 1);

            // Remove quotes from value if present
            if (value.length() >= 2 && (value.front() == '\'' || value.front() == '"') && value.front() == value.back()) {
                value = value.substr(1, value.length() - 2);
            }

            env_vars_[key] = value;
            spdlog::get("EnvReader")->debug("Loaded env var: {}={}", key, value);
        } else {
            spdlog::get("EnvReader")->warn("Skipping malformed line in .env file: {}", line);
        }
    }
    spdlog::get("EnvReader")->info(".env file loaded successfully from: {}", file_path);
    return true;
}

/**
 * @brief Gets the value associated with a given key.
 * @param key The key to look up.
 * @param default_value The value to return if the key is not found.
 * @return The value associated with the key, or the default_value if not found.
 */
std::string EnvReader::getOrDefault(const std::string& key, const std::string& default_value) const {
    auto it = env_vars_.find(key);
    if (it != env_vars_.end()) {
        return it->second;
    }
    spdlog::get("EnvReader")->warn("Key '{}' not found in .env file. Using default value: '{}'", key, default_value);
    return default_value;
}
