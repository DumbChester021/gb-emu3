#pragma once

#include <string>
#include <map>
#include <fstream>
#include <sstream>

/**
 * Config - Simple INI-style configuration manager
 * 
 * Singleton pattern for easy access from anywhere.
 * Stores key-value pairs, persists to/from file.
 */
class Config {
public:
    static Config& Instance() {
        static Config instance;
        return instance;
    }

    void Load(const std::string& filename = "config.ini") {
        m_filename = filename;
        std::ifstream file(filename);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            
            size_t delimiterPos = line.find('=');
            if (delimiterPos != std::string::npos) {
                std::string key = line.substr(0, delimiterPos);
                std::string value = line.substr(delimiterPos + 1);
                m_data[key] = value;
            }
        }
    }

    void Save() {
        if (m_filename.empty()) return;
        std::ofstream file(m_filename);
        if (!file.is_open()) return;

        for (const auto& [key, value] : m_data) {
            file << key << "=" << value << "\n";
        }
    }

    std::string Get(const std::string& key, const std::string& defaultValue = "") const {
        auto it = m_data.find(key);
        if (it != m_data.end()) {
            return it->second;
        }
        return defaultValue;
    }

    void Set(const std::string& key, const std::string& value) {
        m_data[key] = value;
    }

    int GetInt(const std::string& key, int defaultValue = 0) const {
        std::string val = Get(key);
        if (val.empty()) return defaultValue;
        try {
            return std::stoi(val);
        } catch (...) {
            return defaultValue;
        }
    }

    void SetInt(const std::string& key, int value) {
        Set(key, std::to_string(value));
    }
    
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const {
        std::string val = Get(key);
        if (val.empty()) return defaultValue;
        try {
            return std::stof(val);
        } catch (...) {
            return defaultValue;
        }
    }

    void SetFloat(const std::string& key, float value) {
        Set(key, std::to_string(value));
    }

private:
    std::map<std::string, std::string> m_data;
    std::string m_filename;

    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
};
