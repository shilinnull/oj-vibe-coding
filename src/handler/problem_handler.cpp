#include "handler/problem_handler.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "db/dao/problem_dao.h"
#include "db/dao/test_case_dao.h"
#include "handler/handler_base.h"

namespace oj {
namespace handler {

using json = nlohmann::json;

namespace {

std::string NormalizeStatusFilter(std::string status) {
	std::transform(status.begin(), status.end(), status.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return status;
}

}  // namespace

void RegisterProblemRoutes(Router& router, MySqlPool& pool) {
	router.Get(R"(/api/problems)", [&pool](const httplib::Request& req, httplib::Response& res) {
		GuardJsonHandler(res, [&]() {
			const int limit = ClampInt(ParseIntParam(req, "limit", 20), 1, 100);
			const int offset = std::max(0, ParseIntParam(req, "offset", 0));
			const std::string status = req.HasParam("status")
										? NormalizeStatusFilter(req.GetParam("status"))
							: std::string();

			ProblemDao dao(pool);
			json body;
			body["items"] = dao.List(limit, offset, status);
			body["limit"] = limit;
			body["offset"] = offset;
			body["count"] = body["items"].size();
			SendJson(res, 200, body);
		});
	});

	router.Get(R"(/api/problems/(\d+))", [&pool](const httplib::Request& req, httplib::Response& res) {
		GuardJsonHandler(res, [&]() {
			if (req._matches.size() < 2) {
				SendJsonError(res, 400, "invalid problem id");
				return;
			}

			auto id = ParseInt64(req._matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJsonError(res, 400, "invalid problem id");
				return;
			}

			ProblemDao problem_dao(pool);
			auto problem = problem_dao.GetById(*id);
			if (!problem.has_value()) {
				SendJsonError(res, 404, "problem not found");
				return;
			}

			TestCaseDao test_case_dao(pool);
			json body = *problem;
			body["samples"] = test_case_dao.ListByProblem(*id, true);
			SendJson(res, 200, body);
		});
	});
}

}  // namespace handler
}  // namespace oj
