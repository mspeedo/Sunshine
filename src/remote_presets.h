/**
 * @file src/remote_presets.h
 * @brief Host-local named snapshots of the *on-disk* Sunshine configuration.
 *
 * This is deliberately independent of the HTTP layer and of runtime config::
 * globals. A preset is staged on disk; applying it does not change the running
 * encoder until Sunshine's existing restart endpoint is invoked.
 */
#pragma once

#include "file_handler.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace remote_presets {
  namespace fs = std::filesystem;

  // Strict names prevent path traversal, ambiguous URLs, and reserved files.
  inline bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 48 || name.front() == '-') {
      return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
  }

  inline fs::path directory(const fs::path &config_file) {
    return config_file.parent_path() / "config-presets";
  }

  inline fs::path preset_file(const fs::path &config_file, std::string_view name) {
    if (!valid_name(name)) {
      throw std::invalid_argument("Preset name must be 1-48 letters, digits, hyphens, or underscores");
    }
    return directory(config_file) / (std::string(name) + ".conf");
  }

  inline void require_regular_file(const fs::path &file) {
    // Never follow a user-created symlink in the preset store.
    if (!fs::is_regular_file(file) || fs::is_symlink(fs::symlink_status(file))) {
      throw std::runtime_error("Configuration file unavailable or is a symbolic link");
    }
  }

  inline std::vector<std::string> list(const fs::path &config_file) {
    std::vector<std::string> result;
    const auto dir = directory(config_file);
    if (!fs::exists(dir)) {
      return result;
    }
    for (const auto &entry : fs::directory_iterator(dir)) {
      if (!entry.is_regular_file() || entry.is_symlink() || entry.path().extension() != ".conf") {
        continue;
      }
      const auto name = entry.path().stem().string();
      if (valid_name(name)) {
        result.push_back(name);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  }

  inline void save(const fs::path &config_file, std::string_view name, bool overwrite) {
    require_regular_file(config_file);
    const auto target = preset_file(config_file, name);
    fs::create_directories(target.parent_path());
    if (fs::exists(target) && (fs::is_symlink(fs::symlink_status(target)) || !overwrite)) {
      throw std::runtime_error("Preset already exists or cannot be overwritten");
    }
    if (!fs::copy_file(config_file, target, overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none)) {
      throw std::runtime_error("Preset could not be saved");
    }
#ifndef _WIN32
    fs::permissions(target, fs::status(config_file).permissions(), fs::perm_options::replace);
#endif
  }

  inline void apply(const fs::path &config_file, std::string_view name) {
    require_regular_file(config_file);
    const auto preset = preset_file(config_file, name);
    require_regular_file(preset);

    // Preserve the exact text, including comments and any advanced options.
    // A backup of the formerly active file remains for manual recovery.
    const auto backup = fs::path(config_file.string() + ".before-remote-preset.bak");
    if (fs::is_symlink(fs::symlink_status(backup))) {
      throw std::runtime_error("Backup path cannot be a symbolic link");
    }
    fs::copy_file(config_file, backup, fs::copy_options::overwrite_existing);
    const auto replacement = file_handler::read_file(preset.string().c_str());
    if (file_handler::write_file(config_file.string().c_str(), replacement) != 0) {
      // Do not report success after a failed write; best-effort rollback.
      fs::copy_file(backup, config_file, fs::copy_options::overwrite_existing);
      throw std::runtime_error("Could not apply preset; previous configuration restored");
    }
  }

  inline void remove(const fs::path &config_file, std::string_view name) {
    const auto target = preset_file(config_file, name);
    require_regular_file(target);
    if (!fs::remove(target)) {
      throw std::runtime_error("Preset could not be deleted");
    }
  }
}  // namespace remote_presets
