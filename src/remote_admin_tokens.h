#pragma once

// Ephemeral, scoped credentials issued only by the paired-client mTLS GameStream
// endpoint. These are never persisted and are invalidated on pairing changes.
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "crypto.h"

namespace remote_admin_tokens {
  inline constexpr auto lifetime = std::chrono::minutes {30};
  inline constexpr std::size_t max_tokens = 128;
  inline std::mutex mutex;
  inline std::unordered_map<std::string, std::chrono::steady_clock::time_point> issued;

  inline void revoke_all() {
    std::lock_guard lock {mutex};
    issued.clear();
  }

  inline std::string issue() {
    std::lock_guard lock {mutex};
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(issued, [now](const auto &item) { return item.second <= now; });
    // Bound memory if a paired client repeatedly asks for new tokens.
    if (issued.size() >= max_tokens) {
      issued.clear();
    }
    auto token = crypto::rand_alphabet(48);
    issued.insert_or_assign(token, now + lifetime);
    return token;
  }

  inline bool validate(std::string_view token) {
    if (token.size() != 48) {
      return false;
    }
    std::lock_guard lock {mutex};
    const auto it = issued.find(std::string(token));
    if (it == issued.end()) {
      return false;
    }
    if (it->second <= std::chrono::steady_clock::now()) {
      issued.erase(it);
      return false;
    }
    return true;
  }
}  // namespace remote_admin_tokens
