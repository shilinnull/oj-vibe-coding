#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "utils/json_helper.h"

namespace oj {
namespace handler {

using json = nlohmann::json;

inline void SendJson(httplib::Response& res, int status, const json& body) {
	res.status = status;
	res.set_content(body.dump(), "application/json");
}

inline void SendJsonError(httplib::Response& res,
												int status,
												const std::string& error,
												const std::string& message = std::string()) {
	json body{{"error", error}};
	if (!message.empty()) {
		body["message"] = message;
	}
	SendJson(res, status, body);
}

inline std::optional<std::int64_t> ParseInt64(const std::string& text) {
	try {
		size_t pos = 0;
		long long value = std::stoll(text, &pos);
		if (pos != text.size()) {
			return std::nullopt;
		}
		return static_cast<std::int64_t>(value);
	} catch (...) {
		return std::nullopt;
	}
}

inline int ClampInt(int value, int min_value, int max_value) {
	return std::max(min_value, std::min(value, max_value));
}

inline int ParseIntParam(const httplib::Request& req, const char* key, int default_value) {
	if (!req.has_param(key)) {
		return default_value;
	}
	try {
		return std::stoi(req.get_param_value(key));
	} catch (...) {
		return default_value;
	}
}

inline std::optional<json> ParseJsonBody(const httplib::Request& req,
															httplib::Response& res) {
	std::string parse_error;
	auto body = TryParseJson(req.body, &parse_error);
	if (!body.has_value()) {
		SendJsonError(res, 400, "invalid json", parse_error);
		return std::nullopt;
	}
	return body;
}

template <typename Func>
inline void GuardJsonHandler(httplib::Response& res, Func&& fn) {
	try {
		fn();
	} catch (const std::exception& e) {
		SendJsonError(res, 500, "internal", e.what());
	} catch (...) {
		SendJsonError(res, 500, "internal", "unknown exception");
	}
}

}  // namespace handler
}  // namespace oj
