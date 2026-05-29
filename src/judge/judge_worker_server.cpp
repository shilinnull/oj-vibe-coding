#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#include "judge/judge_executor.h"
#include "net/http.hpp"
#include "utils/logger.h"

namespace {

std::string EnvOrDefault(const char* name, const std::string& fallback) {
	if (const char* value = std::getenv(name)) {
		if (*value != '\0') {
			return value;
		}
	}
	return fallback;
}

int ResolvePort(int argc, char* argv[]) {
	if (argc < 2) {
		return -1;
	}
	try {
		int port = std::stoi(argv[1]);
		if (port > 0 && port <= 65535) {
			return port;
		}
	} catch (...) {
	}
	return -1;
}

int ResolveServerPort(int argc, char* argv[]) {
	if (argc < 4) {
		return -1;
	}
	try {
		int port = std::stoi(argv[3]);
		if (port > 0 && port <= 65535) {
			return port;
		}
	} catch (...) {
	}
	return -1;
}

std::string ResolveServerHost(int argc, char* argv[]) {
	if (argc < 3) {
		return std::string();
	}
	if (argv[2] == nullptr || argv[2][0] == '\0') {
		return std::string();
	}
	return argv[2];
}

bool WriteAll(int fd, const std::string& data) {
	size_t sent = 0;
	while (sent < data.size()) {
		ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
		if (n < 0) {
			if (errno == EINTR) continue;
			return false;
		}
		if (n == 0) return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

bool ParseHttpResponse(const std::string& raw, int* status, std::string* body) {
	const std::string header_sep = "\r\n\r\n";
	auto header_end = raw.find(header_sep);
	if (header_end == std::string::npos) {
		return false;
	}

	std::istringstream iss(raw.substr(0, header_end));
	std::string http_version;
	if (!(iss >> http_version >> *status)) {
		return false;
	}
	*body = raw.substr(header_end + header_sep.size());
	return true;
}

bool SendHttpJson(const std::string& host,
				  int port,
				  const std::string& method,
				  const std::string& path,
				  const std::string& body,
				  int* response_status,
				  std::string* response_body) {
	struct addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* result = nullptr;
	const std::string port_str = std::to_string(port);
	if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0) {
		return false;
	}

	int fd = -1;
	for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
		fd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0) continue;
		if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
		::close(fd);
		fd = -1;
	}
	freeaddrinfo(result);
	if (fd < 0) {
		return false;
	}

	std::ostringstream req;
	req << method << " " << path << " HTTP/1.1\r\n";
	req << "Host: " << host << ":" << port << "\r\n";
	req << "Connection: close\r\n";
	req << "Content-Type: application/json;charset=utf-8\r\n";
	req << "Content-Length: " << body.size() << "\r\n\r\n";
	req << body;

	const std::string request = req.str();
	bool ok = WriteAll(fd, request);
	std::string raw_response;
	if (ok) {
		char buf[4096];
		while (true) {
			ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
			if (n > 0) {
				raw_response.append(buf, static_cast<size_t>(n));
				continue;
			}
			if (n == 0) {
				break;
			}
			if (errno == EINTR) {
				continue;
			}
			ok = false;
			break;
		}
	}
	::close(fd);
	if (!ok) {
		return false;
	}
	return ParseHttpResponse(raw_response, response_status, response_body);
}

bool RegisterWorkerToServer(int worker_port,
						const std::string& server_host,
						int server_port) {
	const std::string worker_host = EnvOrDefault("JUDGE_WORKER_HOST", "127.0.0.1");
	const std::string body = std::string("{") +
		"\"host\":\"" + worker_host + "\"," +
		"\"port\":" + std::to_string(worker_port) + "," +
		"\"health_path\":\"/healthz\"," +
		"\"judge_path\":\"/judge\"," +
		"\"timeout_ms\":3000}";

	int status = 0;
	std::string response_body;
	return SendHttpJson(server_host, server_port, "POST", "/api/judge/workers/register", body, &status, &response_body) && status == 200;
}

std::string ReadBody(const HttpRequest& req) {
	return req._body;
}

void Usage(const std::string& proc) {
	std::cerr << "Usage: \n\t" << proc << " <port> <oj_server_host> <oj_server_port>" << std::endl;
	std::cerr << "All startup parameters are required and no default values are used." << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
	if (argc != 4) {
		Usage(argv[0]);
		return 1;
	}

	oj::LoggingConfig log_cfg;
	log_cfg.to_stdout = true;
	oj::Logger::Instance().Init(log_cfg);

	const int port = ResolvePort(argc, argv);
	const std::string server_host = ResolveServerHost(argc, argv);
	const int server_port = ResolveServerPort(argc, argv);
	if (port <= 0 || server_host.empty() || server_port <= 0) {
		Usage(argv[0]);
		return 1;
	}
	OJ_LOG_INFO("judge_worker: listening on port " + std::to_string(port));
	OJ_LOG_INFO("judge_worker: register target " + server_host + ":" + std::to_string(server_port));
	HttpServer svr(port);

	std::thread register_thread([port, server_host, server_port]() {
		for (;;) {
			if (RegisterWorkerToServer(port, server_host, server_port)) {
				OJ_LOG_INFO("judge_worker: registered to oj_server on port " + std::to_string(port));
				return;
			}
			OJ_LOG_WARN("judge_worker: register failed, retrying...");
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	});

	svr.Get("/healthz", [](const HttpRequest&, HttpResponse& resp) {
		resp.SetContent("ok", "text/plain; charset=utf-8");
	});

	svr.Post("/judge", [](const HttpRequest& req, HttpResponse& resp) {
		std::string out_json;
		oj::judge::ExecuteJudgeRequest(ReadBody(req), &out_json);
		resp.SetContent(out_json, "application/json;charset=utf-8");
	});

	svr.Listen();
	if (register_thread.joinable()) {
		register_thread.join();
	}
	return 0;
}
