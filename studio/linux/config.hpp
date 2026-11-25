#pragma once

#include <glib.h>
#include <cmath>
#include <map>
#include <string>

namespace linux_config {
inline std::string directory() {
    return std::string(g_get_user_config_dir()) + "/moleculestudio-native";
}

inline bool parseNumber(const char* input, double& value) {
    char* end = nullptr;
    const auto parsed = g_strtod(input, &end);
    if (end == input) return false;
    while (g_ascii_isspace(*end)) ++end;
    if (*end || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

class Preferences {
    GKeyFile* data_ = g_key_file_new();
    GError* error_ = nullptr;
    std::string path_;
public:
    explicit Preferences(const char* file = "interface.ini") : path_(directory() + "/" + file) {
        g_key_file_load_from_file(data_, path_.c_str(), G_KEY_FILE_KEEP_COMMENTS, &error_);
        if (g_error_matches(error_, G_FILE_ERROR, G_FILE_ERROR_NOENT)) g_clear_error(&error_);
    }
    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;
    ~Preferences() {
        g_clear_error(&error_);
        g_key_file_unref(data_);
    }
    bool flag(const char* group, const char* key, bool fallback) const {
        GError* error = nullptr;
        const bool value = g_key_file_get_boolean(data_, group, key, &error);
        if (!error) return value;
        g_error_free(error);
        return fallback;
    }
    std::string text(const char* group, const char* key) const {
        gchar* value = g_key_file_get_string(data_, group, key, nullptr);
        std::string result = value ? value : "";
        g_free(value);
        return result;
    }
    bool flag(const char* group, const char* key, bool value, std::string* error) {
        g_key_file_set_boolean(data_, group, key, value);
        return save(error);
    }
    bool text(const char* group, const char* key, const std::string& value) {
        if (value.empty()) g_key_file_remove_key(data_, group, key, nullptr);
        else g_key_file_set_string(data_, group, key, value.c_str());
        return save(nullptr);
    }
    bool replaceGroup(const char* group, const std::map<std::string, std::string>& entries, std::string* error) {
        g_key_file_remove_group(data_, group, nullptr);
        for (const auto& entry : entries)
            g_key_file_set_string(data_, group, entry.first.c_str(), entry.second.c_str());
        return save(error);
    }
private:
    bool save(std::string* error) {
        if (!error_ && g_mkdir_with_parents(directory().c_str(), 0700) != 0)
            g_set_error_literal(&error_, G_FILE_ERROR, G_FILE_ERROR_FAILED, "无法创建设置目录");
        if (!error_ && g_key_file_save_to_file(data_, path_.c_str(), &error_)) return true;
        if (error) *error = error_ ? error_->message : "无法写入设置";
        return false;
    }
};
}
