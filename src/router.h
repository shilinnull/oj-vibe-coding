#pragma once

#include <functional>
#include <string>
#include <vector>

#include <httplib.h>

namespace oj {

class Router {
 public:
	using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

	void Get(const std::string& pattern, Handler handler);
	void Post(const std::string& pattern, Handler handler);
	void Put(const std::string& pattern, Handler handler);
	void Delete(const std::string& pattern, Handler handler);

	void Mount(httplib::Server& server) const;

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
