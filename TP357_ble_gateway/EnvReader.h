// EnvReader.h
#ifndef ENV_READER_H
#define ENV_READER_H

#include <string>
#include <map>

/**
 * @brief A utility class to read key-value pairs from a .env file.
 */
class EnvReader {
public:
    /**
     * @brief Loads environment variables from the specified .env file.
     * @param filepath The path to the .env file (e.g., ".env").
     * @return True if the file was successfully read and parsed, false otherwise.
     */
    bool load(const std::string& filepath);

    /**
     * @brief Retrieves the value associated with a given key.
     * @param key The key to look up.
     * @return The value as a string. Returns an empty string if the key is not found.
     */
    std::string get(const std::string& key) const;

    /**
     * @brief Retrieves the value associated with a given key, or a default value if not found.
     * @param key The key to look up.
     * @param default_value The value to return if the key is not found.
     * @return The value as a string, or the default_value if the key is not found.
     */
    std::string getOrDefault(const std::string& key, const std::string& default_value) const;

private:
    std::map<std::string, std::string> env_vars_; ///< Stores the loaded environment variables.
};

#endif // ENV_READER_H
