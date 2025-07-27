// EnvReader.cpp
#include "EnvReader.h"
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm> // For std::remove

/**
 * @brief Loads environment variables from the specified .env file.
 * Lines starting with '#' are treated as comments. Empty lines are ignored.
 * @param filepath The path to the .env file (e.g., ".env").
 * @return True if the file was successfully read and parsed, false otherwise.
 */
bool EnvReader::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open .env file: " << filepath << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Find the position of the first '='
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            // No '=' found, skip this line
            std::cerr << "Warning: Skipping malformed line in .env file (no '=' found): " << line << std::endl;
            continue;
        }

        std::string key = line.substr(0, equals_pos);
        std::string value = line.substr(equals_pos + 1);

        // Remove quotes from value if present (e.g., VALUE="my value")
        if (value.length() >= 2 && ((value.front() == '\'' && value.back() == '\'') || (value.front() == '"' && value.back() == '"'))) {
            value = value.substr(1, value.length() - 2);
        }

        env_vars_[key] = value;
    }

    file.close();
    std::cout << ".env file loaded successfully from: " << filepath << std::endl;
    return true;
}

/**
 * @brief Retrieves the value associated with a given key.
 * @param key The key to look up.
 * @return The value as a string. Returns an empty string if the key is not found.
 */
std::string EnvReader::get(const std::string& key) const {
    auto it = env_vars_.find(key);
    if (it != env_vars_.end()) {
        return it->second;
    }
    return ""; // Return empty string if key not found
}

/**
 * @brief Retrieves the value associated with a given key, or a default value if not found.
 * @param key The key to look up.
 * @param default_value The value to return if the key is not found.
 * @return The value as a string, or the default_value if the key is not found.
 */
std::string EnvReader::getOrDefault(const std::string& key, const std::string& default_value) const {
    std::string value = get(key);
    if (value.empty()) {
        return default_value;
    }
    return value;
}
