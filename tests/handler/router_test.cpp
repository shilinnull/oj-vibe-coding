#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <httplib.h>

#include "router.h"
#include "server.h"

namespace {

int FindFreePort() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return 0;
	}

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		close(fd);
		return 0;
	}

	socklen_t len = sizeof(addr);
	if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
		close(fd);
		return 0;
	}

	const int port = ntohs(addr.sin_port);
	close(fd);
	return port;
}

bool WaitForServer(int port) {
	httplib::Client client("127.0.0.1", port);
	client.set_connection_timeout(1, 0);
	client.set_read_timeout(1, 0);

	for (int i = 0; i < 50; ++i) {
		auto res = client.Get("/healthz");
		if (res) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	return false;
}

class ServerGuard {
 public:
	ServerGuard(httplib::Server& server, int port)
		: server_(server), thread_([&server, port]() { server.listen("127.0.0.1", port); }) {}

	~ServerGuard() {
		server_.stop();
		if (thread_.joinable()) {
			thread_.join();
		}
	}

	ServerGuard(const ServerGuard&) = delete;
	ServerGuard& operator=(const ServerGuard&) = delete;

 private:
	httplib::Server& server_;
	std::thread thread_;
};

}  // namespace

TEST(RouterTest, MountsAllHttpMethods) {
	oj::Router router;
	router.Get("/router-get", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("get", "text/plain; charset=utf-8");
	});
	router.Post("/router-post", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("post", "text/plain; charset=utf-8");
	});
	router.Put("/router-put", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("put", "text/plain; charset=utf-8");
	});
	router.Delete("/router-delete", [](const httplib::Request&, httplib::Response& res) {
		res.set_content("delete", "text/plain; charset=utf-8");
	});

	httplib::Server server;
	router.Mount(server);

	const int port = FindFreePort();
	ASSERT_GT(port, 0);
	ServerGuard guard(server, port);
	ASSERT_TRUE(WaitForServer(port));

	httplib::Client client("127.0.0.1", port);
	client.set_connection_timeout(1, 0);
	client.set_read_timeout(1, 0);

	auto get_res = client.Get("/router-get");
	ASSERT_TRUE(get_res);
	EXPECT_EQ(get_res->status, 200);
	EXPECT_EQ(get_res->body, "get");

	auto post_res = client.Post("/router-post", "payload", "text/plain");
	ASSERT_TRUE(post_res);
	EXPECT_EQ(post_res->status, 200);
	EXPECT_EQ(post_res->body, "post");

	auto put_res = client.Put("/router-put", "payload", "text/plain");
	ASSERT_TRUE(put_res);
	EXPECT_EQ(put_res->status, 200);
	EXPECT_EQ(put_res->body, "put");

	auto delete_res = client.Delete("/router-delete");
	ASSERT_TRUE(delete_res);
	EXPECT_EQ(delete_res->status, 200);
	EXPECT_EQ(delete_res->body, "delete");
}

TEST(HttpServerTest, RegistersHealthCheckRoute) {
	oj::AppConfig cfg;
	cfg.auth.jwt.secret = "test-secret";
	oj::HttpServer server(cfg);
	httplib::Server raw_server;
	server.router().Mount(raw_server);

	const int port = FindFreePort();
	ASSERT_GT(port, 0);
	ServerGuard guard(raw_server, port);
	ASSERT_TRUE(WaitForServer(port));

	httplib::Client client("127.0.0.1", port);
	client.set_connection_timeout(1, 0);
	client.set_read_timeout(1, 0);

	auto res = client.Get("/healthz");
	ASSERT_TRUE(res);
	EXPECT_EQ(res->status, 200);
	EXPECT_EQ(res->body, "ok");
}
