#pragma once

#include <chrono>
#include <cctype>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace test_support {

struct HttpResponse {
	int status{0};
	std::string body;
	std::unordered_map<std::string, std::string> headers;
};

class HttpClient {
 public:
	HttpClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

	void set_connection_timeout(int sec, int usec) {
		connect_timeout_sec_ = sec;
		connect_timeout_usec_ = usec;
	}

	void set_read_timeout(int sec, int usec) {
		read_timeout_sec_ = sec;
		read_timeout_usec_ = usec;
	}

	std::shared_ptr<HttpResponse> Get(const std::string& path) {
		return Request("GET", path, {}, std::string(), std::string());
	}

	std::shared_ptr<HttpResponse> Delete(const std::string& path) {
		return Request("DELETE", path, {}, std::string(), std::string());
	}

	std::shared_ptr<HttpResponse> Post(const std::string& path, const std::string& body, const std::string& content_type) {
		return Request("POST", path, {}, body, content_type);
	}

	std::shared_ptr<HttpResponse> Post(const std::string& path,
								const std::unordered_map<std::string, std::string>& headers,
								const std::string& body,
								const std::string& content_type) {
		return Request("POST", path, headers, body, content_type);
	}

	std::shared_ptr<HttpResponse> Put(const std::string& path, const std::string& body, const std::string& content_type) {
		return Request("PUT", path, {}, body, content_type);
	}

	std::shared_ptr<HttpResponse> Put(const std::string& path,
								 const std::unordered_map<std::string, std::string>& headers,
								 const std::string& body,
								 const std::string& content_type) {
		return Request("PUT", path, headers, body, content_type);
	}

 private:
	static std::optional<sockaddr_in> Resolve(const std::string& host, int port) {
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(static_cast<uint16_t>(port));
		if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
			return addr;
		}
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo* res = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
			return std::nullopt;
		}
		addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
		freeaddrinfo(res);
		return addr;
	}

	bool SetTimeout(int fd, int sec, int usec) {
		struct timeval tv {};
		tv.tv_sec = sec;
		tv.tv_usec = usec;
		return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
		       setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
	}

	std::shared_ptr<HttpResponse> Request(const std::string& method,
								   const std::string& path,
								   const std::unordered_map<std::string, std::string>& headers,
								   const std::string& body,
								   const std::string& content_type) {
		int fd = ::socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			return nullptr;
		}

		auto addr_opt = Resolve(host_, port_);
		if (!addr_opt.has_value()) {
			::close(fd);
			return nullptr;
		}

		if (!SetTimeout(fd, connect_timeout_sec_, connect_timeout_usec_)) {
			::close(fd);
			return nullptr;
		}

		if (::connect(fd, reinterpret_cast<sockaddr*>(&*addr_opt), sizeof(sockaddr_in)) != 0) {
			::close(fd);
			return nullptr;
		}

		if (!SetTimeout(fd, read_timeout_sec_, read_timeout_usec_)) {
			::close(fd);
			return nullptr;
		}

		std::ostringstream oss;
		oss << method << ' ' << path << " HTTP/1.1\r\n";
		oss << "Host: " << host_ << "\r\n";
		oss << "Connection: close\r\n";
		for (const auto& kv : headers) {
			oss << kv.first << ": " << kv.second << "\r\n";
		}
		if (!body.empty()) {
			oss << "Content-Length: " << body.size() << "\r\n";
			if (!content_type.empty()) {
				oss << "Content-Type: " << content_type << "\r\n";
			}
		}
		oss << "\r\n";
		oss << body;
		const std::string request = oss.str();

		size_t sent = 0;
		while (sent < request.size()) {
			ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				::close(fd);
				return nullptr;
			}
			sent += static_cast<size_t>(n);
		}

		::shutdown(fd, SHUT_WR);
		auto response = ReadResponse(fd);
		::close(fd);
		return response;
	}

	std::shared_ptr<HttpResponse> ReadResponse(int fd) {
		std::string data;
		char buffer[4096];
		std::size_t header_end = std::string::npos;
		while (header_end == std::string::npos) {
			ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
			if (n <= 0) {
				return nullptr;
			}
			data.append(buffer, static_cast<std::size_t>(n));
			header_end = data.find("\r\n\r\n");
		}

		auto res = std::make_shared<HttpResponse>();
		std::istringstream iss(data.substr(0, header_end));
		std::string line;
		if (!std::getline(iss, line)) {
			return nullptr;
		}
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		std::istringstream first(line);
		std::string httpver;
		first >> httpver >> res->status;

		while (std::getline(iss, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			auto pos = line.find(':');
			if (pos == std::string::npos) {
				continue;
			}
			std::string key = line.substr(0, pos);
			std::string value = line.substr(pos + 1);
			while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
			while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
			res->headers[key] = value;
		}

		std::size_t content_length = 0;
		auto it = res->headers.find("Content-Length");
		if (it != res->headers.end()) {
			content_length = static_cast<std::size_t>(std::strtoull(it->second.c_str(), nullptr, 10));
		}

		std::string body = data.substr(header_end + 4);
		while (body.size() < content_length) {
			ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
			if (n <= 0) {
				break;
			}
			body.append(buffer, static_cast<std::size_t>(n));
		}
		if (body.size() > content_length) {
			body.resize(content_length);
		}
		res->body = std::move(body);
		return res;
	}

	std::string host_;
	int port_{};
	int connect_timeout_sec_{1};
	int connect_timeout_usec_{0};
	int read_timeout_sec_{1};
	int read_timeout_usec_{0};
};

inline bool WaitForServer(int port) {
	HttpClient client("127.0.0.1", port);
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

template <typename ServerT, typename StartFn>
std::shared_ptr<ServerT> StartDetachedServer(std::shared_ptr<ServerT> server, StartFn start_fn) {
	std::thread([server = std::move(server), start_fn = std::move(start_fn)]() mutable {
		start_fn(*server);
	}).detach();
	return server;
}

}  // namespace test_support
