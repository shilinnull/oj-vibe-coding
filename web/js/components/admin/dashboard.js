import { adminListSubmissions, adminUpdateUserStatus } from "../../api.js";
import { escapeHtml, formatDate, showToast } from "../../utils.js";

function $(id) {
	return document.getElementById(id);
}

function setVisible(el, visible, display = "inline-flex") {
	if (!el) return;
	el.style.display = visible ? display : "none";
}

function renderSubmissions(items) {
	const tbody = $("admin-submissions-tbody");
	if (!tbody) return;
	if (!Array.isArray(items) || items.length === 0) {
		tbody.innerHTML = '<tr><td colspan="6" class="muted">暂无提交</td></tr>';
		return;
	}
	tbody.innerHTML = items
		.map((s) => {
			return `
			<tr>
				<td>${escapeHtml(s.id)}</td>
				<td>${escapeHtml(s.username || s.user_id || "-")}</td>
				<td>${escapeHtml(s.problem_title || s.problem_id || "-")}</td>
				<td>${escapeHtml(s.language_name || s.language_id || "-")}</td>
				<td>${escapeHtml(s.status || "-")}</td>
				<td>${escapeHtml(formatDate(s.created_at || s.createdAt || s.create_time))}</td>
			</tr>`;
		})
		.join("");
}

export function initAdminDashboardPage() {
	let aborted = false;

	async function load() {
		try {
			setVisible($("admin-submissions-loading"), true);
			const payload = await adminListSubmissions({ limit: 20, offset: 0 });
			if (aborted) return;
			renderSubmissions(payload?.items || []);
		} catch (e) {
			showToast(e?.message || "加载失败", "error");
		} finally {
			setVisible($("admin-submissions-loading"), false);
		}
	}

	$("admin-refresh")?.addEventListener("click", load);

	$("admin-user-status-form")?.addEventListener("submit", async (ev) => {
		ev.preventDefault();
		const userId = Number($("admin-user-id")?.value || 0);
		const status = String($("admin-user-status")?.value || "");
		if (!userId || !status) {
			showToast("请输入用户ID并选择状态", "error");
			return;
		}
		try {
			await adminUpdateUserStatus(userId, status);
			showToast("已更新用户状态", "success");
		} catch (e) {
			showToast(e?.message || "更新失败", "error");
		}
	});

	load();

	return () => {
		aborted = true;
	};
}
