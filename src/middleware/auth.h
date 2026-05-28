#pragma once

#include <optional>
#include <string>

#include "net/http.hpp"

namespace oj {

struct AuthInfo {
    long user_id{0};
    std::string username;
    std::string role;
};

// 生成 JWT：服务端只保存密钥，token 本身携带用户身份和过期时间。
std::string GenerateJwt(const std::string& secret, long user_id, const std::string& username, const std::string& role, int expires_seconds);

// 校验 JWT，成功时返回解析出的用户信息。
std::optional<AuthInfo> VerifyJwt(const std::string& secret, const std::string& token);

// 从 Authorization: Bearer xxx 中取 token 并校验。
std::optional<AuthInfo> AuthenticateRequest(const std::string& secret, const http::Request& req);

}  // namespace oj
