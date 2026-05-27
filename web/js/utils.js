const STORAGE_KEYS = {
	token: "oj_token",
	userId: "oj_user_id",
	username: "oj_username",
	role: "oj_role",
};

function base64UrlToBase64(str) {
	return String(str || "").replace(/-/g, "+").replace(/_/g, "/");
}

function decodeJwtPayload(token) {
	try {
		const parts = String(token || "").split(".");
		if (parts.length < 2) return null;
		const b64 = base64UrlToBase64(parts[1]);
		const padded = b64 + "=".repeat((4 - (b64.length % 4)) % 4);
		const jsonText = atob(padded);
		return JSON.parse(jsonText);
	} catch {
		return null;
	}
}

function isJwtExpired(token) {
	const payload = decodeJwtPayload(token);
	if (!payload) {
		return false;
	}
	const exp = Number(payload.exp || 0);
	if (!Number.isFinite(exp) || exp <= 0) {
		return false;
	}
	const now = Math.floor(Date.now() / 1000);
	return now > exp;
}

export function getToken() {
	return localStorage.getItem(STORAGE_KEYS.token) || "";
}

export function setAuth(token, userId, username) {
	if (token) {
		localStorage.setItem(STORAGE_KEYS.token, token);
		const payload = decodeJwtPayload(token);
		if (payload && typeof payload.role === "string") {
			localStorage.setItem(STORAGE_KEYS.role, payload.role);
		} else {
			localStorage.removeItem(STORAGE_KEYS.role);
		}
	}
	if (userId !== undefined && userId !== null) {
		localStorage.setItem(STORAGE_KEYS.userId, String(userId));
	}
	if (username) {
		localStorage.setItem(STORAGE_KEYS.username, username);
	}
}

export function clearAuth() {
	localStorage.removeItem(STORAGE_KEYS.token);
	localStorage.removeItem(STORAGE_KEYS.userId);
	localStorage.removeItem(STORAGE_KEYS.username);
	localStorage.removeItem(STORAGE_KEYS.role);
}

export function getCurrentUser() {
	const token = getToken();
	if (token && isJwtExpired(token)) {
		clearAuth();
		return {
			token: "",
			username: "",
			userId: 0,
			role: "",
			isAuthed: false,
		};
	}
	const username = localStorage.getItem(STORAGE_KEYS.username) || "";
	const userId = Number(localStorage.getItem(STORAGE_KEYS.userId) || 0);
	let role = localStorage.getItem(STORAGE_KEYS.role) || "";
	if (!role && token) {
		const payload = decodeJwtPayload(token);
		if (payload && typeof payload.role === "string") {
			role = payload.role;
			localStorage.setItem(STORAGE_KEYS.role, role);
		}
	}
	return {
		token,
		username,
		userId: Number.isFinite(userId) ? userId : 0,
		role,
		isAuthed: Boolean(token),
	};
}

export function isAdmin() {
	const u = getCurrentUser();
	return Boolean(u && u.isAuthed && u.role === "admin");
}

export function parseHashRoute() {
	const raw = window.location.hash.replace(/^#/, "") || "/";
	const [pathPart, queryString = ""] = raw.split("?");
	const query = Object.fromEntries(new URLSearchParams(queryString).entries());
	return {
		path: pathPart || "/",
		query,
	};
}

export function toHash(path, query = {}) {
	const cleanPath = path.startsWith("/") ? path : `/${path}`;
	const search = new URLSearchParams();
	Object.entries(query).forEach(([k, v]) => {
		if (v !== undefined && v !== null && String(v) !== "") {
			search.set(k, String(v));
		}
	});
	const q = search.toString();
	return `#${cleanPath}${q ? `?${q}` : ""}`;
}

export function escapeHtml(text) {
	return String(text || "")
		.replaceAll("&", "&amp;")
		.replaceAll("<", "&lt;")
		.replaceAll(">", "&gt;")
		.replaceAll('"', "&quot;")
		.replaceAll("'", "&#39;");
}

export function formatDate(ts) {
	if (!ts) {
		return "-";
	}
	const d = new Date(ts);
	if (Number.isNaN(d.getTime())) {
		return String(ts);
	}
	const date = d.toLocaleDateString("zh-CN");
	const time = d.toLocaleTimeString("zh-CN", { hour12: false });
	return `${date} ${time}`;
}

export function normalizeStatus(status) {
	const s = String(status || "").toLowerCase();
	if (!s) {
		return { text: "UNKNOWN", className: "status-wait" };
	}
	if (s.includes("accept") || s === "ac") {
		return { text: "ACCEPTED", className: "status-ok" };
	}
	if (s.includes("pending") || s.includes("running")) {
		return { text: s.toUpperCase(), className: "status-wait" };
	}
	if (s.includes("wrong") || s.includes("time_limit") || s.includes("memory_limit")) {
		return { text: s.toUpperCase(), className: "status-warn" };
	}
	if (s.includes("compile") || s.includes("runtime") || s.includes("system") || s === "re" || s === "ce") {
		return { text: s.toUpperCase(), className: "status-danger" };
	}
	return { text: s.toUpperCase(), className: "status-wait" };
}

export function debounce(fn, waitMs = 300) {
	let timer = 0;
	return (...args) => {
		window.clearTimeout(timer);
		timer = window.setTimeout(() => fn(...args), waitMs);
	};
}

export function showToast(message, type = "info") {
	const root = document.getElementById("toast-root");
	if (!root) {
		return;
	}
	const toast = document.createElement("div");
	toast.className = `toast ${type === "error" ? "error" : type === "success" ? "success" : ""}`;
	toast.textContent = message;
	root.appendChild(toast);
	window.setTimeout(() => {
		toast.remove();
	}, 2800);
}

export function setLoading(el, text = "加载中...") {
	if (!el) {
		return;
	}
	el.innerHTML = `<div class="loading"><span class="spinner"></span><span>${escapeHtml(text)}</span></div>`;
}
