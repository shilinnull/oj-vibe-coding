#include "handler/problem_handler.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "db/dao/problem_dao.h"
#include "db/dao/test_case_dao.h"
#include "utils/json_helper.h"

namespace oj {
namespace handler {

using json = nlohmann::json;

namespace {

void SendJson(httplib::Response& res, int status, const json& body) {
	res.status = status;
	res.set_content(body.dump(), "application/json");
}

std::optional<std::int64_t> ParseInt64(const std::string& text) {
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

int ClampInt(int value, int min_value, int max_value) {
	return std::max(min_value, std::min(value, max_value));
}

int ParseIntParam(const httplib::Request& req, const char* key, int default_value) {
	if (!req.has_param(key)) {
		return default_value;
	}
	try {
		return std::stoi(req.get_param_value(key));
	} catch (...) {
		return default_value;
	}
}

std::string NormalizeStatusFilter(std::string status) {
	std::transform(status.begin(), status.end(), status.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return status;
}

}  // namespace

void RegisterProblemRoutes(Router& router, MySqlPool& pool) {
	router.Get(R"(/api/problems)", [&pool](const httplib::Request& req, httplib::Response& res) {
		try {
			const int limit = ClampInt(ParseIntParam(req, "limit", 20), 1, 100);
			const int offset = std::max(0, ParseIntParam(req, "offset", 0));
			const std::string status = req.has_param("status")
							? NormalizeStatusFilter(req.get_param_value("status"))
							: std::string();

			ProblemDao dao(pool);
			json body;
			body["items"] = dao.List(limit, offset, status);
			body["limit"] = limit;
			body["offset"] = offset;
			body["count"] = body["items"].size();
			SendJson(res, 200, body);
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Get(R"(/api/problems/(\d+))", [&pool](const httplib::Request& req, httplib::Response& res) {
		try {
			if (req.matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}

			auto id = ParseInt64(req.matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}

			ProblemDao problem_dao(pool);
			auto problem = problem_dao.GetById(*id);
			if (!problem.has_value()) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}

			TestCaseDao test_case_dao(pool);
			json body = *problem;
			body["samples"] = test_case_dao.ListByProblem(*id, true);
			SendJson(res, 200, body);
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});
}

}  // namespace handler
}  // namespace oj
