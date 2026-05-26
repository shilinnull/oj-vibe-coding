import { logout as apiLogout } from "./api.js";
import { initLoginPage, initRegisterPage } from "./components/login.js";
import { initProblemListPage } from "./components/problem-list.js";
import { initProblemDetailPage } from "./components/problem-detail.js";
import { initSubmissionResultPage } from "./components/submission-result.js";
import { initHistoryPage } from "./components/history.js";
import { initAdminDashboardPage } from "./components/admin/dashboard.js";
import { initAdminProblemEditPage } from "./components/admin/problem-edit.js";
import { initAdminLanguageConfigPage } from "./components/admin/language-config.js";
import { clearAuth, getCurrentUser, isAdmin, parseHashRoute, showToast, toHash } from "./utils.js";

const app = document.getElementById("app");
const userBadge = document.getElementById("user-badge");
const logoutBtn = document.getElementById("logout-btn");
const nav = document.getElementById("topnav");
const adminLink = document.getElementById("admin-link");

const routeTemplates = {
	login: "./pages/login.html",
	register: "./pages/register.html",
	problemList: "./pages/problem-list.html",
	problemDetail: "./pages/problem-detail.html",
	submissionResult: "./pages/submission-result.html",
	history: "./pages/history.html",
	adminDashboard: "./pages/admin/dashboard.html",
	adminProblemEdit: "./pages/admin/problem-edit.html",
	adminLanguageConfig: "./pages/admin/language-config.html",
};

const templateCache = new Map();
let cleanupCurrentView = null;

function setUserUI() {
	const user = getCurrentUser();
	userBadge.textContent = user.isAuthed ? `${user.username || "用户"}` : "未登录";
	logoutBtn.style.display = user.isAuthed ? "inline-flex" : "none";
	if (adminLink) {
		adminLink.style.display = isAdmin() ? "inline-flex" : "none";
	}
}

function resolveRoute(path) {
	if (path === "/" || path === "") {
		return { name: "root", params: {} };
	}
	if (path === "/login") {
		return { name: "login", params: {} };
	}
	if (path === "/register") {
		return { name: "register", params: {} };
	}
	if (path === "/problems") {
		return { name: "problemList", params: {} };
	}
	if (/^\/problems\/\d+$/.test(path)) {
		return { name: "problemDetail", params: { id: path.split("/").at(-1) } };
	}
	if (/^\/submissions\/\d+$/.test(path)) {
		return { name: "submissionResult", params: { id: path.split("/").at(-1) } };
	}
	if (path === "/history") {
		return { name: "history", params: {} };
	}
	if (path === "/admin") {
		return { name: "adminDashboard", params: {} };
	}
	if (path === "/admin/problems") {
		return { name: "adminProblemEdit", params: {} };
	}
	if (path === "/admin/languages") {
		return { name: "adminLanguageConfig", params: {} };
	}
	return { name: "notFound", params: {} };
}

async function loadTemplate(name) {
	if (!routeTemplates[name]) {
		return "<div class=\"card\"><h2>页面不存在</h2><p class=\"muted\">请检查地址。</p></div>";
	}
	if (templateCache.has(name)) {
		return templateCache.get(name);
	}
	const rsp = await fetch(routeTemplates[name]);
	if (!rsp.ok) {
		throw new Error(`模板加载失败: ${routeTemplates[name]}`);
	}
	const html = await rsp.text();
	templateCache.set(name, html);
	return html;
}

function navigate(path, query = {}) {
	window.location.hash = toHash(path, query);
}

async function render() {
	const { path, query } = parseHashRoute();
	const route = resolveRoute(path);
	const user = getCurrentUser();
	const isPublicPage = route.name === "login" || route.name === "register";

	if (route.name === "root") {
		navigate(user.isAuthed ? "/problems" : "/login");
		return;
	}
	if (!user.isAuthed && !isPublicPage) {
		navigate("/login");
		return;
	}
	if (route.name.startsWith("admin") && !isAdmin()) {
		showToast("需要管理员权限", "error");
		navigate("/problems");
		return;
	}

	try {
		if (typeof cleanupCurrentView === "function") {
			cleanupCurrentView();
			cleanupCurrentView = null;
		}

		app.innerHTML = await loadTemplate(route.name);
		markActiveNav(path);

		const ctx = {
			route,
			query,
			navigate,
			refreshUser: setUserUI,
		};

		switch (route.name) {
			case "login":
				cleanupCurrentView = initLoginPage(ctx);
				break;
			case "register":
				cleanupCurrentView = initRegisterPage(ctx);
				break;
			case "problemList":
				cleanupCurrentView = initProblemListPage(ctx);
				break;
			case "problemDetail":
				cleanupCurrentView = initProblemDetailPage(ctx);
				break;
			case "submissionResult":
				cleanupCurrentView = initSubmissionResultPage(ctx);
				break;
			case "history":
				cleanupCurrentView = initHistoryPage(ctx);
				break;
			case "adminDashboard":
				cleanupCurrentView = initAdminDashboardPage(ctx);
				break;
			case "adminProblemEdit":
				cleanupCurrentView = initAdminProblemEditPage(ctx);
				break;
			case "adminLanguageConfig":
				cleanupCurrentView = initAdminLanguageConfigPage(ctx);
				break;
			default:
				app.innerHTML = "<div class=\"card\"><h2>404</h2><p class=\"muted\">未找到页面。</p></div>";
		}
	} catch (error) {
		app.innerHTML = `<div class="error-box">${error.message || "页面加载失败"}</div>`;
	}
}

function markActiveNav(path) {
	if (!nav) {
		return;
	}
	const all = nav.querySelectorAll("a");
	all.forEach((a) => {
		const href = a.getAttribute("href") || "";
		const target = href.replace(/^#/, "");
		const active = path.startsWith(target) && target !== "/";
		a.classList.toggle("active", active);
	});
}

function bindGlobalEvents() {
	document.body.addEventListener("click", (event) => {
		const el = event.target.closest("[data-link]");
		if (!el) {
			return;
		}
		const href = el.getAttribute("href") || "";
		if (!href.startsWith("#")) {
			return;
		}
		event.preventDefault();
		window.location.hash = href;
	});

	logoutBtn?.addEventListener("click", async () => {
		try {
			await apiLogout();
		} catch {
			// 忽略登出失败，前端仍清理凭证。
		}
		clearAuth();
		setUserUI();
		showToast("已退出登录", "success");
		navigate("/login");
	});

	window.addEventListener("hashchange", render);
}

setUserUI();
bindGlobalEvents();
render();
