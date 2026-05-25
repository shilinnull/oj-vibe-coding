#include "handler/submission_handler.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "db/dao/language_dao.h"
#include "db/dao/problem_dao.h"
#include "db/dao/submission_dao.h"
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

std::optional<std::int64_t> ParseJsonInt64(const json& value) {
	if (value.is_number_integer()) {
		return value.get<std::int64_t>();
	}
	if (value.is_string()) {
		return ParseInt64(value.get<std::string>());
	}
	return std::nullopt;
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

std::string Lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
}

std::optional<json> ParseRequestBody(const httplib::Request& req, std::string* error) {
	return TryParseJson(req.body, error);
}

std::optional<std::int64_t> ResolveUserId(const httplib::Request& req, const json& body) {
	if (body.contains("user_id")) {
		auto id = ParseJsonInt64(body.at("user_id"));
		if (id.has_value()) {
			return id;
		}
	}
	if (req.has_param("user_id")) {
		return ParseInt64(req.get_param_value("user_id"));
	}
	if (req.has_header("X-User-Id")) {
		return ParseInt64(req.get_header_value("X-User-Id"));
	}
	return std::nullopt;
}

std::optional<std::int64_t> ExtractRequiredId(const json& body, const char* key) {
	if (!body.contains(key)) {
		return std::nullopt;
	}
	return ParseJsonInt64(body.at(key));
}

json SubmissionToResponse(const Submission& submission) {
	json body = submission;
	if (!submission.result_json.empty()) {
		if (auto parsed = TryParseJson(submission.result_json, nullptr); parsed.has_value()) {
			body["result"] = *parsed;
		}
	}
	return body;
}

}  // namespace

void RegisterSubmissionRoutes(Router& router, MySqlPool& pool) {
	router.Post(R"(/api/submissions)", [&pool](const httplib::Request& req, httplib::Response& res) {
		try {
			std::string parse_error;
			auto body_json = ParseRequestBody(req, &parse_error);
			if (!body_json.has_value()) {
				SendJson(res, 400, json{{"error", "invalid json"}, {"message", parse_error}});
				return;
			}

			const json& body = *body_json;
			const std::string source_code = body.value("source_code", std::string());
			auto problem_id = ExtractRequiredId(body, "problem_id");
			auto language_id = ExtractRequiredId(body, "language_id");
			auto user_id = ResolveUserId(req, body);
			const std::string mode = Lower(body.value("mode", std::string("submit")));

			if (source_code.empty() || !problem_id.has_value() || !language_id.has_value() || !user_id.has_value() || *user_id <= 0) {
				SendJson(res, 400, json{{"error", "source_code, problem_id, language_id and user_id are required"}});
				return;
			}
			if (mode != "run" && mode != "submit") {
				SendJson(res, 400, json{{"error", "mode must be run or submit"}});
				return;
			}

			ProblemDao problem_dao(pool);
			if (!problem_dao.GetById(*problem_id).has_value()) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}

			LanguageDao language_dao(pool);
			auto language = language_dao.GetById(static_cast<int>(*language_id));
			if (!language.has_value()) {
				SendJson(res, 404, json{{"error", "language not found"}});
				return;
			}
			if (!language->enabled) {
				SendJson(res, 400, json{{"error", "language disabled"}});
				return;
			}

			TestCaseDao test_case_dao(pool);
			const auto samples = test_case_dao.ListByProblem(*problem_id, true);

			Submission submission;
			submission.user_id = *user_id;
			submission.problem_id = *problem_id;
			submission.language_id = static_cast<int>(*language_id);
			submission.source_code = source_code;
			submission.mode = mode;
			submission.status = "pending";
			submission.result_json = json{{"status", "PENDING"},
													{"mode", mode},
													{"judge_scope", mode == "run" ? "sample" : "all"},
													{"sample_case_count", samples.size()}}
							.dump();
			const std::int64_t submission_id = SubmissionDao(pool).Create(submission);

			json response = SubmissionToResponse(submission);
			response["id"] = submission_id;
			response["sample_case_count"] = samples.size();
			SendJson(res, 201, response);
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Get(R"(/api/submissions/(\d+))", [&pool](const httplib::Request& req, httplib::Response& res) {
		try {
			if (req.matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid submission id"}});
				return;
			}

			auto id = ParseInt64(req.matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJson(res, 400, json{{"error", "invalid submission id"}});
				return;
			}

			SubmissionDao dao(pool);
			auto submission = dao.GetById(*id);
			if (!submission.has_value()) {
				SendJson(res, 404, json{{"error", "submission not found"}});
				return;
			}

			SendJson(res, 200, SubmissionToResponse(*submission));
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Get(R"(/api/submissions)", [&pool](const httplib::Request& req, httplib::Response& res) {
		try {
			if (!req.has_param("user_id")) {
				SendJson(res, 400, json{{"error", "user_id is required"}});
				return;
			}

			auto user_id = ParseInt64(req.get_param_value("user_id"));
			if (!user_id.has_value() || *user_id <= 0) {
				SendJson(res, 400, json{{"error", "invalid user_id"}});
				return;
			}

			const int limit = ClampInt(ParseIntParam(req, "limit", 20), 1, 100);
			const int offset = std::max(0, ParseIntParam(req, "offset", 0));

			SubmissionDao dao(pool);
			json body;
			body["items"] = dao.ListByUser(*user_id, limit, offset);
			body["limit"] = limit;
			body["offset"] = offset;
			body["user_id"] = *user_id;
			body["count"] = body["items"].size();
			SendJson(res, 200, body);
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});
}

}  // namespace handler
}  // namespace oj
