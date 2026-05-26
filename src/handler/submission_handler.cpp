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
#include "handler/handler_base.h"

namespace oj {
namespace handler {

using json = nlohmann::json;

namespace {

std::optional<std::int64_t> ParseJsonInt64(const json& value) {
	if (value.is_number_integer()) {
		return value.get<std::int64_t>();
	}
	if (value.is_string()) {
		return ParseInt64(value.get<std::string>());
	}
	return std::nullopt;
}

std::string Lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return s;
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
	router.Get(R"(/api/languages)", [&pool](const httplib::Request& /*req*/, httplib::Response& res) {
		GuardJsonHandler(res, [&]() {
			LanguageDao dao(pool);
			SendJson(res, 200, json{{"items", dao.ListAll(true)}});
		});
	});

	router.Post(R"(/api/submissions)", [&pool](const httplib::Request& req, httplib::Response& res) {
		GuardJsonHandler(res, [&]() {
			auto body_json = ParseJsonBody(req, res);
			if (!body_json.has_value()) {
				return;
			}

			const json& body = *body_json;
			const std::string source_code = body.value("source_code", std::string());
			auto problem_id = ExtractRequiredId(body, "problem_id");
			auto language_id = ExtractRequiredId(body, "language_id");
			auto user_id = ResolveUserId(req, body);
			const std::string mode = Lower(body.value("mode", std::string("submit")));

			if (source_code.empty() || !problem_id.has_value() || !language_id.has_value() || !user_id.has_value() || *user_id <= 0) {
				SendJsonError(res, 400, "source_code, problem_id, language_id and user_id are required");
				return;
			}
			if (mode != "run" && mode != "submit") {
				SendJsonError(res, 400, "mode must be run or submit");
				return;
			}

			ProblemDao problem_dao(pool);
			if (!problem_dao.GetById(*problem_id).has_value()) {
				SendJsonError(res, 404, "problem not found");
				return;
			}

			LanguageDao language_dao(pool);
			auto language = language_dao.GetById(static_cast<int>(*language_id));
			if (!language.has_value()) {
				SendJsonError(res, 404, "language not found");
				return;
			}
			if (!language->enabled) {
				SendJsonError(res, 400, "language disabled");
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
		});
	});

	router.Get(R"(/api/submissions/(\d+))", [&pool](const httplib::Request& req, httplib::Response& res) {
		GuardJsonHandler(res, [&]() {
			if (req.matches.size() < 2) {
				SendJsonError(res, 400, "invalid submission id");
				return;
			}

			auto id = ParseInt64(req.matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJsonError(res, 400, "invalid submission id");
				return;
			}

			SubmissionDao dao(pool);
			auto submission = dao.GetById(*id);
			if (!submission.has_value()) {
				SendJsonError(res, 404, "submission not found");
				return;
			}

			SendJson(res, 200, SubmissionToResponse(*submission));
		});
	});

	router.Get(R"(/api/submissions)", [&pool](const httplib::Request& req, httplib::Response& res) {
		GuardJsonHandler(res, [&]() {
			if (!req.has_param("user_id")) {
				SendJsonError(res, 400, "user_id is required");
				return;
			}

			auto user_id = ParseInt64(req.get_param_value("user_id"));
			if (!user_id.has_value() || *user_id <= 0) {
				SendJsonError(res, 400, "invalid user_id");
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
		});
	});
}

}  // namespace handler
}  // namespace oj
