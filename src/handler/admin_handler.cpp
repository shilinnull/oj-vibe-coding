
#include "handler/admin_handler.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "db/dao/language_dao.h"
#include "db/dao/problem_dao.h"
#include "db/dao/submission_dao.h"
#include "db/dao/test_case_dao.h"
#include "db/dao/user_dao.h"
#include "middleware/auth.h"
#include "utils/json_helper.h"

namespace oj {
namespace handler {

using json = nlohmann::json;

namespace {

void SendJson(http::Response& res, int status, const json& body) {
	res._statu = status;
	res.SetContent(body.dump(), "application/json");
}

std::optional<std::int64_t> ParseInt64(const std::string& text) {
	try {
		size_t pos = 0;
		long long value = std::stoll(text, &pos);
		if (pos != text.size()) return std::nullopt;
		return static_cast<std::int64_t>(value);
	} catch (...) {
		return std::nullopt;
	}
}

int ClampInt(int value, int min_value, int max_value) {
	return std::max(min_value, std::min(value, max_value));
}

int ParseIntParam(const http::Request& req, const char* key, int default_value) {
	if (!req.HasParam(key)) return default_value;
	try {
		return std::stoi(req.GetParam(key));
	} catch (...) {
		return default_value;
	}
}

std::optional<AuthInfo> RequireAdmin(const http::Request& req,
											http::Response& res,
											const std::string& jwt_secret) {
	auto info = AuthenticateRequest(jwt_secret, req);
	if (!info.has_value()) {
		SendJson(res, 401, json{{"error", "unauthorized"}});
		return std::nullopt;
	}
	if (info->role != "admin") {
		SendJson(res, 403, json{{"error", "forbidden"}});
		return std::nullopt;
	}
	return info;
}

std::optional<json> ParseBody(const http::Request& req, http::Response& res) {
	std::string error;
	auto body = TryParseJson(req._body, &error);
	if (!body.has_value()) {
		SendJson(res, 400, json{{"error", "invalid json"}, {"message", error}});
		return std::nullopt;
	}
	return body;
}

std::string ReadAll(const std::filesystem::path& path) {
	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) {
		throw std::runtime_error("failed to read file: " + path.string());
	}
	std::string content;
	ifs.seekg(0, std::ios::end);
	auto size = ifs.tellg();
	if (size < 0) size = 0;
	content.resize(static_cast<size_t>(size));
	ifs.seekg(0, std::ios::beg);
	ifs.read(content.data(), static_cast<std::streamsize>(content.size()));
	return content;
}

void WriteAll(const std::filesystem::path& path, const std::string& bytes) {
	std::ofstream ofs(path, std::ios::binary);
	if (!ofs) {
		throw std::runtime_error("failed to write file: " + path.string());
	}
	ofs.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

int RunProcess(const std::vector<std::string>& argv) {
	if (argv.empty()) return -1;
	std::vector<char*> cargv;
	cargv.reserve(argv.size() + 1);
	for (const auto& s : argv) {
		cargv.push_back(const_cast<char*>(s.c_str()));
	}
	cargv.push_back(nullptr);

	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		execvp(cargv[0], cargv.data());
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) return -1;
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	return -1;
}

std::filesystem::path MakeTempDir(const std::string& prefix) {
	std::filesystem::path base = std::filesystem::temp_directory_path();
	std::string tmpl = (base / (prefix + "XXXXXX")).string();
	std::vector<char> buf(tmpl.begin(), tmpl.end());
	buf.push_back('\0');
	char* out = mkdtemp(buf.data());
	if (!out) {
		throw std::runtime_error(std::string("mkdtemp failed: ") + std::strerror(errno));
	}
	return std::filesystem::path(out);
}

struct ZipParsedCase {
	int sort_order{0};
	std::string input;
	std::string output;
};

std::vector<ZipParsedCase> ParseTestcasesFromDir(const std::filesystem::path& dir) {
	std::unordered_map<int, std::filesystem::path> ins;
	std::unordered_map<int, std::filesystem::path> outs;

	for (auto it = std::filesystem::recursive_directory_iterator(dir);
		 it != std::filesystem::recursive_directory_iterator();
		 ++it) {
		if (!it->is_regular_file()) continue;
		auto p = it->path();
		auto ext = p.extension().string();
		if (ext != ".in" && ext != ".out") continue;
		std::string stem = p.stem().string();
		int idx = 0;
		try {
			idx = std::stoi(stem);
		} catch (...) {
			continue;
		}
		if (idx <= 0) continue;
		if (ext == ".in") {
			ins[idx] = p;
		} else {
			outs[idx] = p;
		}
	}

	std::vector<int> ids;
	ids.reserve(ins.size());
	for (const auto& kv : ins) ids.push_back(kv.first);
	std::sort(ids.begin(), ids.end());

	std::vector<ZipParsedCase> cases;
	for (int idx : ids) {
		auto it_out = outs.find(idx);
		if (it_out == outs.end()) continue;
		ZipParsedCase tc;
		tc.sort_order = idx;
		tc.input = ReadAll(ins[idx]);
		tc.output = ReadAll(it_out->second);
		cases.push_back(std::move(tc));
	}
	return cases;
}

}  // namespace

void RegisterAdminRoutes(Router& router, const std::string& jwt_secret, MySqlPool& pool) {
	// Problems CRUD
	router.Post(R"(/api/admin/problems)", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			auto body_opt = ParseBody(req, res);
			if (!body_opt.has_value()) return;
			const json& body = *body_opt;

			Problem p;
			p.title = body.value("title", std::string());
			p.description = body.value("description", std::string());
			p.difficulty = body.value("difficulty", std::string("medium"));
			p.time_limit_ms = body.value("time_limit_ms", 1000);
			p.memory_limit_kb = body.value("memory_limit_kb", 262144);
			p.status = body.value("status", std::string("draft"));
			p.created_by = admin->user_id;

			if (p.title.empty() || p.description.empty()) {
				SendJson(res, 400, json{{"error", "title and description required"}});
				return;
			}

			ProblemDao dao(pool);
			const std::int64_t id = dao.Create(p);
			p.id = id;
			SendJson(res, 201, json(p));
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Get(R"(/api/admin/problems/(\d+))", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto id = ParseInt64(req._matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			ProblemDao pdao(pool);
			auto p = pdao.GetById(*id);
			if (!p.has_value()) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}
			TestCaseDao tcdao(pool);
			json body = *p;
			body["test_cases"] = tcdao.ListByProblem(*id, false);
			SendJson(res, 200, body);
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Put(R"(/api/admin/problems/(\d+))", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto id = ParseInt64(req._matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto body_opt = ParseBody(req, res);
			if (!body_opt.has_value()) return;
			const json& body = *body_opt;

			Problem p;
			p.title = body.value("title", std::string());
			p.description = body.value("description", std::string());
			p.difficulty = body.value("difficulty", std::string("medium"));
			p.time_limit_ms = body.value("time_limit_ms", 1000);
			p.memory_limit_kb = body.value("memory_limit_kb", 262144);
			p.status = body.value("status", std::string("draft"));
			p.created_by = admin->user_id;

			if (p.title.empty() || p.description.empty()) {
				SendJson(res, 400, json{{"error", "title and description required"}});
				return;
			}

			ProblemDao dao(pool);
			if (!dao.Update(*id, p)) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}
			SendJson(res, 200, json{{"ok", true}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Delete(R"(/api/admin/problems/(\d+))", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto id = ParseInt64(req._matches[1]);
			if (!id.has_value() || *id <= 0) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			ProblemDao dao(pool);
			if (!dao.Delete(*id)) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}
			SendJson(res, 200, json{{"ok", true}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	// Testcases add
	router.Post(R"(/api/admin/problems/(\d+)/testcases)", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto pid = ParseInt64(req._matches[1]);
			if (!pid.has_value() || *pid <= 0) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto body_opt = ParseBody(req, res);
			if (!body_opt.has_value()) return;
			const json& body = *body_opt;
			TestCase tc;
			tc.problem_id = *pid;
			tc.is_sample = body.value("is_sample", false);
			tc.input = body.value("input", std::string());
			tc.output = body.value("output", std::string());
			tc.sort_order = body.value("sort_order", 0);

			if (tc.input.empty() && tc.output.empty()) {
				SendJson(res, 400, json{{"error", "input/output required"}});
				return;
			}
			ProblemDao pdao(pool);
			if (!pdao.GetById(*pid).has_value()) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}

			TestCaseDao dao(pool);
			const std::int64_t id = dao.Add(tc);
			SendJson(res, 201, json{{"id", id}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	// Testcases zip upload (replace all non-sample cases)
	router.Post(R"(/api/admin/problems/(\d+)/testcases/upload)",
						 [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			auto pid = ParseInt64(req._matches[1]);
			if (!pid.has_value() || *pid <= 0) {
				SendJson(res, 400, json{{"error", "invalid problem id"}});
				return;
			}
			ProblemDao pdao(pool);
			if (!pdao.GetById(*pid).has_value()) {
				SendJson(res, 404, json{{"error", "problem not found"}});
				return;
			}

			std::string zip_bytes;
			std::string filename;
			if (req.IsMultipartFormData() && req.HasFile("file")) {
				auto file = req.GetFileValue("file");
				zip_bytes = file.content;
				filename = file.filename;
			} else {
				zip_bytes = req._body;
			}
			if (zip_bytes.empty()) {
				SendJson(res, 400, json{{"error", "zip file required"}});
				return;
			}

			auto tmp = MakeTempDir("oj_zip_");
			auto zip_path = tmp / "cases.zip";
			auto extract_dir = tmp / "extract";
			std::filesystem::create_directories(extract_dir);
			WriteAll(zip_path, zip_bytes);

			int unzip_rc = RunProcess({"unzip", "-o", "-qq", zip_path.string(), "-d", extract_dir.string()});
			if (unzip_rc != 0) {
				SendJson(res, 400, json{{"error", "unzip failed"}, {"code", unzip_rc}});
				std::error_code ec;
				std::filesystem::remove_all(tmp, ec);
				return;
			}

			auto parsed = ParseTestcasesFromDir(extract_dir);
			if (parsed.empty()) {
				SendJson(res, 400, json{{"error", "no valid *.in/*.out pairs found"}});
				std::error_code ec;
				std::filesystem::remove_all(tmp, ec);
				return;
			}

			TestCaseDao tcdao(pool);
			const int deleted = tcdao.DeleteByProblem(*pid, true);
			int inserted = 0;
			for (const auto& item : parsed) {
				TestCase tc;
				tc.problem_id = *pid;
				tc.is_sample = false;
				tc.input = item.input;
				tc.output = item.output;
				tc.sort_order = item.sort_order;
				tcdao.Add(tc);
				inserted++;
			}

			std::error_code ec;
			std::filesystem::remove_all(tmp, ec);
			SendJson(res,
					200,
					json{{"ok", true},
						 {"deleted_non_sample", deleted},
						 {"inserted", inserted},
						 {"filename", filename}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	// Languages CRUD
	router.Get(R"(/api/admin/languages)", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			( void )req;
			LanguageDao dao(pool);
			SendJson(res, 200, json{{"items", dao.ListAll(false)}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Post(R"(/api/admin/languages)", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			auto body_opt = ParseBody(req, res);
			if (!body_opt.has_value()) return;
			const json& body = *body_opt;

			Language lang;
			lang.name = body.value("name", std::string());
			lang.extension = body.value("extension", std::string());
			lang.compile_cmd = body.value("compile_cmd", std::string());
			lang.run_cmd = body.value("run_cmd", std::string());
			lang.enabled = body.value("enabled", true);

			if (lang.name.empty() || lang.extension.empty() || lang.compile_cmd.empty() || lang.run_cmd.empty()) {
				SendJson(res, 400, json{{"error", "name/extension/compile_cmd/run_cmd required"}});
				return;
			}

			LanguageDao dao(pool);
			int id = dao.Create(lang);
			SendJson(res, 201, json{{"id", id}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	router.Put(R"(/api/admin/languages/(\d+))", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid language id"}});
				return;
			}
			int id = 0;
			try {
				id = std::stoi(req._matches[1]);
			} catch (...) {
				SendJson(res, 400, json{{"error", "invalid language id"}});
				return;
			}
			auto body_opt = ParseBody(req, res);
			if (!body_opt.has_value()) return;
			const json& body = *body_opt;

			Language lang;
			lang.name = body.value("name", std::string());
			lang.extension = body.value("extension", std::string());
			lang.compile_cmd = body.value("compile_cmd", std::string());
			lang.run_cmd = body.value("run_cmd", std::string());
			lang.enabled = body.value("enabled", true);

			if (lang.name.empty() || lang.extension.empty() || lang.compile_cmd.empty() || lang.run_cmd.empty()) {
				SendJson(res, 400, json{{"error", "name/extension/compile_cmd/run_cmd required"}});
				return;
			}

			LanguageDao dao(pool);
			if (!dao.Update(id, lang)) {
				SendJson(res, 404, json{{"error", "language not found"}});
				return;
			}
			SendJson(res, 200, json{{"ok", true}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	// Admin submissions list
	router.Get(R"(/api/admin/submissions)", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			const int limit = ClampInt(ParseIntParam(req, "limit", 20), 1, 100);
			const int offset = std::max(0, ParseIntParam(req, "offset", 0));
			SubmissionDao dao(pool);
			json body;
			body["items"] = dao.ListAll(limit, offset);
			body["limit"] = limit;
			body["offset"] = offset;
			body["count"] = body["items"].size();
			SendJson(res, 200, body);
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});

	// User status management
	router.Put(R"(/api/admin/users/(\d+)/status)", [&pool, &jwt_secret](const http::Request& req, http::Response& res) {
		try {
			auto admin = RequireAdmin(req, res, jwt_secret);
			if (!admin.has_value()) return;
			( void )admin;
			if (req._matches.size() < 2) {
				SendJson(res, 400, json{{"error", "invalid user id"}});
				return;
			}
			auto uid = ParseInt64(req._matches[1]);
			if (!uid.has_value() || *uid <= 0) {
				SendJson(res, 400, json{{"error", "invalid user id"}});
				return;
			}
			auto body_opt = ParseBody(req, res);
			if (!body_opt.has_value()) return;
			const json& body = *body_opt;
			std::string status = body.value("status", std::string());
			if (status != "active" && status != "banned") {
				SendJson(res, 400, json{{"error", "status must be active or banned"}});
				return;
			}
			UserDao dao(pool);
			if (!dao.UpdateStatus(*uid, status)) {
				SendJson(res, 404, json{{"error", "user not found"}});
				return;
			}
			SendJson(res, 200, json{{"ok", true}});
		} catch (const std::exception& e) {
			SendJson(res, 500, json{{"error", "internal"}, {"message", e.what()}});
		}
	});
}

}  // namespace handler
}  // namespace oj
