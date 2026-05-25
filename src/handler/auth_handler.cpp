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

static void RegisterHandler(const httplib::Request& req, httplib::Response& res, MySqlPool& pool, const std::string& jwt_secret) {
    try {
        auto body = json::parse(req.body);
        std::string username = body.value("username", std::string());
        std::string password = body.value("password", std::string());
        std::string email = body.value("email", std::string());
        if (username.empty() || password.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"username and password required"})", "application/json");
            return;
        }
        MySqlPool& p = pool;
        UserDao dao(p);
        if (dao.GetByUsername(username).has_value()) {
            res.status = 409;
            res.set_content(R"({"error":"user exists"})", "application/json");
            return;
        }
        std::string hash = GeneratePasswordHash(password);
        long id = dao.CreateUser(username, hash, email, "student", "active");
        std::string token = GenerateJwt(jwt_secret, id, username, "student", 3600);
        json out = { {"id", id}, {"token", token} };
        res.set_content(out.dump(), "application/json");
    } catch (const std::exception& e) {
        OJ_LOG_ERROR(std::string("register error: ") + e.what());
        res.status = 500;
        res.set_content(R"({"error":"internal"})", "application/json");
    }
}

static void LoginHandler(const httplib::Request& req, httplib::Response& res, MySqlPool& pool, const std::string& jwt_secret) {
    try {
        auto body = json::parse(req.body);
        std::string username = body.value("username", std::string());
        std::string password = body.value("password", std::string());
        if (username.empty() || password.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"username and password required"})", "application/json");
            return;
        }
        UserDao dao(pool);
        auto uopt = dao.GetByUsername(username);
        if (!uopt.has_value()) {
            res.status = 401;
            res.set_content(R"({"error":"invalid credentials"})", "application/json");
            return;
        }
        auto u = *uopt;
        if (!VerifyPassword(password, u.password)) {
            res.status = 401;
            res.set_content(R"({"error":"invalid credentials"})", "application/json");
            return;
        }
        std::string token = GenerateJwt(jwt_secret, u.id, u.username, u.role, 3600);
        json out = { {"id", u.id}, {"token", token} };
        res.set_content(out.dump(), "application/json");
    } catch (const std::exception& e) {
        OJ_LOG_ERROR(std::string("login error: ") + e.what());
        res.status = 500;
        res.set_content(R"({"error":"internal"})", "application/json");
    }
}

static void LogoutHandler(const httplib::Request& /*req*/, httplib::Response& res) {
    // Stateless JWT: logout is a no-op unless you implement token blacklist
    res.set_content(R"({"ok":true})", "application/json");
}

void RegisterAuthRoutes(Router& router, const std::string& jwt_secret, MySqlPool& pool) {
    router.Post("/api/auth/register", [&](const httplib::Request& req, httplib::Response& res) {
        RegisterHandler(req, res, pool, jwt_secret);
    });
    router.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
        LoginHandler(req, res, pool, jwt_secret);
    });
    router.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res) {
        LogoutHandler(req, res);
    });
}

}  // namespace handler
}  // namespace oj
