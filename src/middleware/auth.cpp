#include "middleware/auth.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include "utils/crypto.h"

namespace oj {

using json = nlohmann::json;

std::string GenerateJwt(const std::string& secret, long user_id, const std::string& username, const std::string& role, int expires_seconds) {
    json header = { {"alg", "HS256"}, {"typ", "JWT"} };
    auto now = std::chrono::system_clock::now();
    auto now_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    json payload = {
        {"sub", user_id},
        {"username", username},
        {"role", role},
        {"iat", now_s},
        {"exp", now_s + expires_seconds}
    };
    std::string header_b64 = Base64UrlEncode(header.dump());
    std::string payload_b64 = Base64UrlEncode(payload.dump());
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string sig = HmacSha256Base64Url(secret, signing_input);
    return signing_input + "." + sig;
}

std::optional<AuthInfo> VerifyJwt(const std::string& secret, const std::string& token) {
    // split
    size_t p1 = token.find('.');
    if (p1 == std::string::npos) return std::nullopt;
    size_t p2 = token.find('.', p1 + 1);
    if (p2 == std::string::npos) return std::nullopt;
    std::string header_b64 = token.substr(0, p1);
    std::string payload_b64 = token.substr(p1 + 1, p2 - (p1 + 1));
    std::string sig = token.substr(p2 + 1);
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string expected_sig;
    try {
        expected_sig = HmacSha256Base64Url(secret, signing_input);
    } catch (...) {
        return std::nullopt;
    }
    if (expected_sig != sig) return std::nullopt;
    auto payload_json_opt = Base64UrlDecode(payload_b64);
    if (!payload_json_opt) return std::nullopt;
    try {
        auto payload = json::parse(*payload_json_opt);
        long exp = payload.value("exp", 0L);
        auto now = std::chrono::system_clock::now();
        auto now_s = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        if (exp != 0 && now_s > exp) return std::nullopt;
        AuthInfo info;
        info.user_id = payload.value("sub", 0L);
        info.username = payload.value("username", std::string());
        info.role = payload.value("role", std::string());
        return info;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AuthInfo> AuthenticateRequest(const std::string& secret, const httplib::Request& req) {
    auto it = req.headers.find("Authorization");
    if (it == req.headers.end()) return std::nullopt;
    const std::string& v = it->second;
    const std::string prefix = "Bearer ";
    if (v.size() <= prefix.size() || v.substr(0, prefix.size()) != prefix) return std::nullopt;
    std::string token = v.substr(prefix.size());
    return VerifyJwt(secret, token);
}

}  // namespace oj
