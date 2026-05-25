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
		"Content-Type": "application/json",
		...(options.headers || {}),
	};
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

export async function getLanguageOptions() {
	// 当前后端没有开放公开语言列表，这里给出常见默认值并支持本地扩展。
	return [
		{ id: 1, name: "C++17", monacoLang: "cpp", extension: "cpp" },
		{ id: 2, name: "C11", monacoLang: "c", extension: "c" },
	];
}
