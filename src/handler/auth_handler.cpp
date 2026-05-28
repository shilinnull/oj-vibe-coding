#include "handler/auth_handler.h"

#include <nlohmann/json.hpp>

#include "db/mysql_pool.h"
#include "db/dao/user_dao.h"
#include "middleware/auth.h"
#include "utils/crypto.h"
#include "utils/config.h"
#include "utils/logger.h"

namespace oj {
namespace handler {

using json = nlohmann::json;

static void RegisterHandler(const http::Request& req, http::Response& res, MySqlPool& pool, const std::string& jwt_secret) {
    try {
        // 注册接口只接受用户名、密码和可选邮箱，其他字段由服务端补齐。
        auto body = json::parse(req._body);
        std::string username = body.value("username", std::string());
        std::string password = body.value("password", std::string());
        std::string email = body.value("email", std::string());
        if (username.empty() || password.empty()) {
            res._statu = 400;
            res.SetContent(R"({"error":"username and password required"})", "application/json");
            return;
        }
        MySqlPool& p = pool;
        UserDao dao(p);
        if (dao.GetByUsername(username).has_value()) {
            res._statu = 409;
            res.SetContent(R"({"error":"user exists"})", "application/json");
            return;
        }
        std::string hash = GeneratePasswordHash(password);
        // 新用户默认是 student / active，登录成功后直接返回 token。
        long id = dao.CreateUser(username, hash, email, "student", "active");
        std::string token = GenerateJwt(jwt_secret, id, username, "student", 3600);
        json out = { {"id", id}, {"token", token} };
        res.SetContent(out.dump(), "application/json");
    } catch (const std::exception& e) {
        OJ_LOG_ERROR(std::string("register error: ") + e.what());
        res._statu = 500;
        res.SetContent(R"({"error":"internal"})", "application/json");
    }
}

static void LoginHandler(const http::Request& req, http::Response& res, MySqlPool& pool, const std::string& jwt_secret) {
    try {
        // 登录先查用户，再比对密码哈希，避免明文密码入库和传递。
        auto body = json::parse(req._body);
        std::string username = body.value("username", std::string());
        std::string password = body.value("password", std::string());
        if (username.empty() || password.empty()) {
            res._statu = 400;
            res.SetContent(R"({"error":"username and password required"})", "application/json");
            return;
        }
        UserDao dao(pool);
        auto uopt = dao.GetByUsername(username);
        if (!uopt.has_value()) {
            res._statu = 401;
            res.SetContent(R"({"error":"invalid credentials"})", "application/json");
            return;
        }
        auto u = *uopt;
        if (!VerifyPassword(password, u.password)) {
            res._statu = 401;
            res.SetContent(R"({"error":"invalid credentials"})", "application/json");
            return;
        }
        std::string token = GenerateJwt(jwt_secret, u.id, u.username, u.role, 3600);
        json out = { {"id", u.id}, {"token", token} };
        res.SetContent(out.dump(), "application/json");
    } catch (const std::exception& e) {
        OJ_LOG_ERROR(std::string("login error: ") + e.what());
        res._statu = 500;
        res.SetContent(R"({"error":"internal"})", "application/json");
    }
}

static void LogoutHandler(const http::Request& /*req*/, http::Response& res) {
    // JWT 是无状态的：如果不做 token 黑名单，退出本质上只是前端删 token。
    res.SetContent(R"({"ok":true})", "application/json");
}

void RegisterAuthRoutes(Router& router, const std::string& jwt_secret, MySqlPool& pool) {
    // 认证相关接口统一放在 /api/auth 下，方便前端和网关记忆。
    router.Post("/api/auth/register", [&](const http::Request& req, http::Response& res) {
        RegisterHandler(req, res, pool, jwt_secret);
    });
    router.Post("/api/auth/login", [&](const http::Request& req, http::Response& res) {
        LoginHandler(req, res, pool, jwt_secret);
    });
    router.Post("/api/auth/logout", [&](const http::Request& req, http::Response& res) {
        LogoutHandler(req, res);
    });
}

}  // namespace handler
}  // namespace oj
