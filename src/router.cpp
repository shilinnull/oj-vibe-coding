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

void Router::Mount(httplib::Server& server) const {
	for (const auto& r : routes_) {
		switch (r.method) {
			case Method::Get:
				server.Get(r.pattern, r.handler);
				break;
			case Method::Post:
				server.Post(r.pattern, r.handler);
				break;
			case Method::Put:
				server.Put(r.pattern, r.handler);
				break;
			case Method::Delete:
				server.Delete(r.pattern, r.handler);
				break;
		}
	}
}

}  // namespace oj
