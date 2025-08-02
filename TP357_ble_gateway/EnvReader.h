// EnvReader.h
#ifndef ENV_READER_H
#define ENV_READER_H

#include <string>
#include <map>

// spdlog include
#include "spdlog/spdlog.h"

/**
 * @brief A utility class to read key-value pairs from a .env file.
 */
class EnvReader {
public:
    /**
     * @brief Loads environment variables from the specified .env file.
     * @param file_path The path to the .env file.
     * @return True if the file was loaded successfully, false otherwise.
     */
    bool load(const std::string& file_path);

    /**
     * @brief Gets the value associated with a given key.
     * @param key The key to look up.
     * @param default_value The value to return if the key is not found.
     * @return The value associated with the key, or the default_value if not found.
     */
    std::string getOrDefault(const std::string& key, const std::string& default_value) const;

private:
    std::map<std::string, std::string> env_vars_; ///< Stores the loaded environment variables.
};

#endif // ENV_READER_H
