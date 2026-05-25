const STORAGE_KEYS = {
	token: "oj_token",
	userId: "oj_user_id",
	username: "oj_username",
};

export function getToken() {
	return localStorage.getItem(STORAGE_KEYS.token) || "";
}

export function setAuth(token, userId, username) {
	if (token) {
		localStorage.setItem(STORAGE_KEYS.token, token);
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
}

export function getCurrentUser() {
	const token = getToken();
	const username = localStorage.getItem(STORAGE_KEYS.username) || "";
	const userId = Number(localStorage.getItem(STORAGE_KEYS.userId) || 0);
	return {
		token,
		username,
		userId: Number.isFinite(userId) ? userId : 0,
		isAuthed: Boolean(token),
	};
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
