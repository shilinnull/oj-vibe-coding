#pragma once

#include <functional>
#include <string>
#include <vector>

#include "http.hpp"

class HttpServer;

namespace oj {

// 轻量级路由表：先把路由和回调保存起来，最后统一挂到 HTTP server 上。
class Router {
 public:
	using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

	// 下面四个接口只负责登记路由，不会立刻处理请求。
	void Get(const std::string& pattern, Handler handler);
	void Post(const std::string& pattern, Handler handler);
	void Put(const std::string& pattern, Handler handler);
	void Delete(const std::string& pattern, Handler handler);

	void Mount(::HttpServer& server) const;

 private:
	enum class Method { Get, Post, Put, Delete };
	struct Route {
		Method method;
		std::string pattern;
		Handler handler;
	};

	std::vector<Route> routes_;
};

}  // namespace oj
