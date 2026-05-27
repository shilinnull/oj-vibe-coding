#include "router.h"

namespace oj {

void Router::Get(const std::string& pattern, Handler handler) {
	routes_.push_back(Route{Method::Get, pattern, std::move(handler)});
}

void Router::Post(const std::string& pattern, Handler handler) {
	routes_.push_back(Route{Method::Post, pattern, std::move(handler)});
}

void Router::Put(const std::string& pattern, Handler handler) {
	routes_.push_back(Route{Method::Put, pattern, std::move(handler)});
}

void Router::Delete(const std::string& pattern, Handler handler) {
	routes_.push_back(Route{Method::Delete, pattern, std::move(handler)});
}

void Router::Mount(::HttpServer& server) const {
	for (const auto& route : routes_) {
		switch (route.method) {
			case Method::Get:
				server.Get(route.pattern, route.handler);
				break;
			case Method::Post:
				server.Post(route.pattern, route.handler);
				break;
			case Method::Put:
				server.Put(route.pattern, route.handler);
				break;
			case Method::Delete:
				server.Delete(route.pattern, route.handler);
				break;
		}
	}
}

}  // namespace oj
