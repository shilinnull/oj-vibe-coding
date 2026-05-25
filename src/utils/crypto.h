#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace oj {

// Generate a password hash using PBKDF2-HMAC-SHA256.
// Stored format: iterations:salt_base64:hash_base64
std::string GeneratePasswordHash(const std::string& password);
bool VerifyPassword(const std::string& password, const std::string& stored);

// HMAC-SHA256, returns base64url-encoded digest
std::string HmacSha256Base64Url(const std::string& key, const std::string& data);

// Base64url helpers
std::string Base64UrlEncode(const std::string& data);
std::optional<std::string> Base64UrlDecode(const std::string& b64url);

}  // namespace oj
