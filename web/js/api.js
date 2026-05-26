import { getToken } from "./utils.js";

const API_BASE = "/api";

export class ApiError extends Error {
	constructor(message, status = 0, payload = null) {
		super(message);
		this.name = "ApiError";
		this.status = status;
		this.payload = payload;
	}
}

function toErrorMessage(payload, fallback = "请求失败") {
	if (!payload) {
		return fallback;
	}
	if (typeof payload === "string") {
		return payload;
	}
	if (payload.error && payload.message) {
		return `${payload.error}: ${payload.message}`;
	}
	return payload.error || payload.message || fallback;
}

async function request(path, options = {}) {
	const token = getToken();
	const headers = {
		...(options.headers || {}),
	};
	const isFormData =
		typeof FormData !== "undefined" &&
		options.body &&
		options.body instanceof FormData;
	const hasContentType = Object.keys(headers).some((k) => k.toLowerCase() === "content-type");
	if (!hasContentType && options.body && !isFormData && typeof options.body === "string") {
		headers["Content-Type"] = "application/json";
	}
	if (token) {
		headers.Authorization = `Bearer ${token}`;
	}
	const response = await fetch(`${API_BASE}${path}`, {
		...options,
		headers,
	});

	const bodyText = await response.text();
	let payload = null;
	if (bodyText) {
		try {
			payload = JSON.parse(bodyText);
		} catch {
			payload = bodyText;
		}
	}

	if (!response.ok) {
		throw new ApiError(toErrorMessage(payload), response.status, payload);
	}

	return payload;
}

export function login(username, password) {
	return request("/auth/login", {
		method: "POST",
		body: JSON.stringify({ username, password }),
	});
}

export function register(username, password, email) {
	return request("/auth/register", {
		method: "POST",
		body: JSON.stringify({ username, password, email }),
	});
}

export function logout() {
	return request("/auth/logout", {
		method: "POST",
		body: JSON.stringify({}),
	});
}

export function getProblems({ limit = 12, offset = 0, status = "published" } = {}) {
	const params = new URLSearchParams({
		limit: String(limit),
		offset: String(offset),
	});
	if (status) {
		params.set("status", status);
	}
	return request(`/problems?${params.toString()}`);
}

export function getProblem(problemId) {
	return request(`/problems/${problemId}`);
}

export function createSubmission({ source_code, problem_id, language_id, user_id, mode }) {
	return request("/submissions", {
		method: "POST",
		body: JSON.stringify({ source_code, problem_id, language_id, user_id, mode }),
	});
}

export function getSubmission(submissionId) {
	return request(`/submissions/${submissionId}`);
}

export function getSubmissionsByUser(userId, { limit = 20, offset = 0 } = {}) {
	const params = new URLSearchParams({
		user_id: String(userId),
		limit: String(limit),
		offset: String(offset),
	});
	return request(`/submissions?${params.toString()}`);
}

export function listLanguages() {
	return request("/languages");
}

function mapLanguageMonaco(lang) {
	const ext = String(lang.extension || "").toLowerCase();
	if (ext === "cpp" || ext === "cc" || ext === "cxx") return "cpp";
	if (ext === "c") return "c";
	return "plaintext";
}

export async function getLanguageOptions() {
	try {
		const payload = await listLanguages();
		const items = Array.isArray(payload?.items) ? payload.items : [];
		return items.map((lang) => ({
			...lang,
			monacoLang: mapLanguageMonaco(lang),
		}));
	} catch {
		return [
			{ id: 1, name: "C++17", monacoLang: "cpp", extension: "cpp" },
			{ id: 2, name: "C11", monacoLang: "c", extension: "c" },
		];
	}
}

// ---------------- Admin API ----------------

export function adminCreateProblem(problem) {
	return request("/admin/problems", {
		method: "POST",
		body: JSON.stringify(problem),
	});
}

export function adminGetProblem(problemId) {
	return request(`/admin/problems/${problemId}`);
}

export function adminUpdateProblem(problemId, problem) {
	return request(`/admin/problems/${problemId}`,
		{
			method: "PUT",
			body: JSON.stringify(problem),
		});
}

export function adminDeleteProblem(problemId) {
	return request(`/admin/problems/${problemId}`, { method: "DELETE" });
}

export function adminAddTestcase(problemId, testcase) {
	return request(`/admin/problems/${problemId}/testcases`, {
		method: "POST",
		body: JSON.stringify(testcase),
	});
}

export function adminUploadTestcasesZip(problemId, file) {
	const fd = new FormData();
	fd.append("file", file);
	return request(`/admin/problems/${problemId}/testcases/upload`, {
		method: "POST",
		body: fd,
	});
}

export function adminListLanguages() {
	return request("/admin/languages");
}

export function adminCreateLanguage(lang) {
	return request("/admin/languages", {
		method: "POST",
		body: JSON.stringify(lang),
	});
}

export function adminUpdateLanguage(langId, lang) {
	return request(`/admin/languages/${langId}`, {
		method: "PUT",
		body: JSON.stringify(lang),
	});
}

export function adminListSubmissions({ limit = 20, offset = 0 } = {}) {
	const params = new URLSearchParams({
		limit: String(limit),
		offset: String(offset),
	});
	return request(`/admin/submissions?${params.toString()}`);
}

export function adminUpdateUserStatus(userId, status) {
	return request(`/admin/users/${userId}/status`, {
		method: "PUT",
		body: JSON.stringify({ status }),
	});
}
