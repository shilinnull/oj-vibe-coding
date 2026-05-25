import { getSubmissionsByUser } from "../api.js";
import { escapeHtml, formatDate, getCurrentUser, normalizeStatus, setLoading, showToast } from "../utils.js";

export function initHistoryPage(ctx) {
	const tbody = document.getElementById("history-body");
	const pagerInfoEl = document.getElementById("history-pager-info");
	const prevBtn = document.getElementById("history-prev");
	const nextBtn = document.getElementById("history-next");
	const userTitle = document.getElementById("history-user");

	const user = getCurrentUser();
	if (!user.userId) {
		tbody.innerHTML = "<tr><td colspan=\"8\">请先登录</td></tr>";
		return null;
	}
	userTitle.textContent = `${user.username || "用户"}`;

	let limit = 15;
	let offset = 0;
	let currentItems = [];

	async function refresh() {
		setLoading(tbody, "加载提交历史...");
		prevBtn.disabled = true;
		nextBtn.disabled = true;
		try {
			const data = await getSubmissionsByUser(user.userId, { limit, offset });
			currentItems = Array.isArray(data.items) ? data.items : [];

			if (!currentItems.length) {
				tbody.innerHTML = '<tr><td colspan="8" class="muted">暂无提交记录</td></tr>';
			} else {
				tbody.innerHTML = currentItems
					.map((item) => {
						const ns = normalizeStatus(item.status);
						return `
							<tr>
								<td><a href="#/submissions/${item.id}" data-link>#${item.id}</a></td>
								<td>${item.problem_id ?? "-"}</td>
								<td>${item.language_id ?? "-"}</td>
								<td>${escapeHtml(item.mode || "-")}</td>
								<td><span class="status-badge ${ns.className}">${escapeHtml(ns.text)}</span></td>
								<td>${item.time_ms ?? "-"}</td>
								<td>${item.memory_kb ?? "-"}</td>
								<td>${formatDate(item.created_at)}</td>
							</tr>
						`;
					})
					.join("");
			}
			pagerInfoEl.textContent = `第 ${Math.floor(offset / limit) + 1} 页`;
			prevBtn.disabled = offset <= 0;
			nextBtn.disabled = currentItems.length < limit;
		} catch (error) {
			tbody.innerHTML = `<tr><td colspan="8"><div class="error-box">${escapeHtml(error.message)}</div></td></tr>`;
			showToast(error.message || "历史记录加载失败", "error");
		}
	}

	const onPrev = () => {
		offset = Math.max(0, offset - limit);
		refresh();
	};
	const onNext = () => {
		offset += limit;
		refresh();
	};

	prevBtn?.addEventListener("click", onPrev);
	nextBtn?.addEventListener("click", onNext);

	refresh();

	return () => {
		prevBtn?.removeEventListener("click", onPrev);
		nextBtn?.removeEventListener("click", onNext);
	};
}
