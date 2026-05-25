#pragma once

#include <optional>
#include <string>

#include <httplib.h>

namespace oj {

struct AuthInfo {
    long user_id{0};
    std::string username;
    std::string role;
};

// Generate JWT token (HS256) with expiration seconds
std::string GenerateJwt(const std::string& secret, long user_id, const std::string& username, const std::string& role, int expires_seconds);

// Verify JWT token and return AuthInfo if valid
std::optional<AuthInfo> VerifyJwt(const std::string& secret, const std::string& token);

// Convenience: extract token from Authorization header and verify
std::optional<AuthInfo> AuthenticateRequest(const std::string& secret, const httplib::Request& req);

}  // namespace oj
