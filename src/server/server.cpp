#include "server.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "net/http.hpp"
#include "utils/logger.h"
#include "handler/auth_handler.h"
#include "handler/admin_handler.h"
#include "handler/problem_handler.h"
#include "handler/submission_handler.h"

namespace oj {

namespace {

std::string ResolveExistingPath(std::initializer_list<const char*> candidates) {
	for (const char* candidate : candidates) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec)) {
			return std::filesystem::path(candidate).lexically_normal().string();
		}
	}
	return std::string();
}

bool IsApiPath(const std::string& path) {
	return path == "/api" || path.rfind("/api/", 0) == 0;
}

std::string HtmlEscape(std::string text) {
	auto replace_all = [](std::string& s, const std::string& from, const std::string& to) {
		size_t pos = 0;
		while ((pos = s.find(from, pos)) != std::string::npos) {
			s.replace(pos, from.size(), to);
			pos += to.size();
		}
	};
	replace_all(text, "&", "&amp;");
	replace_all(text, "<", "&lt;");
	replace_all(text, ">", "&gt;");
	replace_all(text, "\"", "&quot;");
	return text;
}

void SendApiError(HttpResponse& res, int status, const std::string& error, const std::string& message) {
	nlohmann::json body{{"error", error}};
	if (!message.empty()) {
		body["message"] = message;
	}
	res._statu = status;
	res.SetContent(body.dump(), "application/json");
}

void SendHtmlError(HttpResponse& res, int status, const std::string& title, const std::string& message) {
	std::ostringstream html;
	html << "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
			 << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
			 << "<title>" << status << " " << HtmlEscape(title) << "</title>"
			 << "<style>body{font-family:ui-sans-serif,system-ui,-apple-system,segoe ui,Roboto,sans-serif;background:#f7f7f8;color:#1f2937;margin:0;}"
			 << ".wrap{max-width:760px;margin:12vh auto;padding:24px;}"
			 << ".card{background:#fff;border:1px solid #e5e7eb;border-radius:12px;padding:24px;box-shadow:0 8px 30px rgba(0,0,0,.05);}"
			 << "h1{margin:0 0 8px;font-size:28px;}p{line-height:1.6;margin:0;color:#4b5563;}"
			 << "a{color:#0ea5e9;text-decoration:none;}a:hover{text-decoration:underline;}</style></head><body>"
			 << "<main class=\"wrap\"><section class=\"card\"><h1>" << status << " " << HtmlEscape(title)
			 << "</h1><p>" << HtmlEscape(message)
			 << "</p><p style=\"margin-top:14px\"><a href=\"/\">返回首页</a></p></section></main></body></html>";
	res._statu = status;
	res.SetContent(html.str(), "text/html; charset=utf-8");
}

std::string ReadTextFile(const std::string& path) {
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		return std::string();
	}
	std::ostringstream oss;
	oss << ifs.rdbuf();
	return oss.str();
}

}  // namespace


HttpServer::HttpServer(const oj::AppConfig& cfg) {
	// 健康检查接口，给部署环境和容器探活用。
	router_.Get("/healthz", [](const http::Request&, http::Response& res) {
		res.SetContent("ok", "text/plain; charset=utf-8");
	});

	// 服务启动时先初始化数据库连接池，再把认证相关路由挂进去。
	try {
		pool_ = std::make_unique<MySqlPool>(cfg.mysql);
		judge_manager_ = std::make_unique<JudgeManager>(*pool_, cfg.judge.max_concurrency);
		handler::RegisterAuthRoutes(router_, cfg.auth.jwt.secret, *pool_);
		handler::RegisterProblemRoutes(router_, *pool_);
		handler::RegisterSubmissionRoutes(router_, *pool_, *judge_manager_);
		handler::RegisterAdminRoutes(router_, cfg.auth.jwt.secret, *pool_);
	} catch (const std::exception& e) {
		OJ_LOG_ERROR(std::string("failed init db or auth routes: ") + e.what());
		throw;
	}
}

void HttpServer::Listen(const char* host, int port) {
	(void)host;
	::HttpServer muduo_server(port);
	std::unordered_map<std::string, std::string> default_headers{{
		{"X-Content-Type-Options", "nosniff"},
		{"X-Frame-Options", "SAMEORIGIN"},
		{"Referrer-Policy", "strict-origin-when-cross-origin"},
	}};
	muduo_server.SetDefaultHeaders(default_headers);

	std::string web_dir = ResolveExistingPath({"./web", "../web", "../../web"});
	std::string index_html;
	if (!web_dir.empty()) {
		index_html = ReadTextFile((std::filesystem::path(web_dir) / "index.html").string());
		muduo_server.SetBaseDir(web_dir);
		OJ_LOG_INFO(std::string("mounted static web dir: ") + web_dir);
	} else {
		OJ_LOG_WARN("web directory not found; static frontend is unavailable");
	}
	const std::string index_html_copy = index_html;

	muduo_server.SetExceptionHandler([index_html_copy](const HttpRequest& req,
																			HttpResponse* res,
																			std::exception_ptr ep) {
		std::string message = "unexpected exception";
		try {
			if (ep) {
				std::rethrow_exception(ep);
				}
		} catch (const std::exception& e) {
			message = e.what();
		}
		OJ_LOG_ERROR(std::string("unhandled exception on ") + req._path + ": " + message);
		if (IsApiPath(req._path)) {
			SendApiError(*res, 500, "internal", message);
		} else {
			SendHtmlError(*res, 500, "Internal Server Error", "服务器发生异常，请稍后重试。");
		}
	});

	muduo_server.SetErrorHandler([index_html_copy](const HttpRequest& req, HttpResponse* res) {
		if (res == nullptr) {
			return;
		}
		if (res->_statu == 404 && !IsApiPath(req._path) && req._method == "GET" && !index_html_copy.empty()) {
			res->_statu = 200;
			res->SetContent(index_html_copy, "text/html; charset=utf-8");
			return;
		}
		if (IsApiPath(req._path)) {
			std::string err = res->_statu == 404 ? "not found" : "request failed";
			SendApiError(*res, res->_statu == 0 ? 500 : res->_statu, err, std::string());
			return;
		}
		const int status = res->_statu == 0 ? 500 : res->_statu;
		if (status == 404) {
			SendHtmlError(*res, 404, "Not Found", "请求的页面不存在。");
		} else {
			SendHtmlError(*res, status, "Request Failed", "请求处理失败，请检查后重试。");
		}
	});

	router_.Mount(muduo_server);
	// listen() 会阻塞，所以这里就是服务真正开始接收请求的地方。
	OJ_LOG_INFO(std::string("listening on ") + host + ":" + std::to_string(port));
	muduo_server.Listen();
}

}  // namespace oj
