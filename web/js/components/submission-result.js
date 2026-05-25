import { getSubmission } from "../api.js";
import { escapeHtml, normalizeStatus, setLoading, showToast } from "../utils.js";

function isFinished(status) {
	const s = String(status || "").toLowerCase();
	return !(s.includes("pending") || s.includes("running"));
}

function renderCaseRows(results) {
	if (!Array.isArray(results) || !results.length) {
		return "<tr><td colspan=\"6\" class=\"muted\">暂无用例结果</td></tr>";
	}
	return results
		.map((item) => {
			const ns = normalizeStatus(item.status);
			return `
			<tr>
				<td>${item.test_case_id ?? "-"}</td>
				<td><span class="status-badge ${ns.className}">${escapeHtml(ns.text)}</span></td>
				<td>${item.time_ms ?? "-"}</td>
				<td>${item.memory_kb ?? "-"}</td>
				<td>${escapeHtml(item.expected_output ?? "")}</td>
				<td>${escapeHtml(item.actual_output ?? "")}</td>
			</tr>
		`;
		})
		.join("");
}

export function initSubmissionResultPage(ctx) {
	const submissionId = Number(ctx.route.params.id || 0);
	const titleEl = document.getElementById("result-title");
	const statusEl = document.getElementById("result-status");
	const loadingEl = document.getElementById("result-loading");
	const summaryEl = document.getElementById("result-summary");
	const caseBody = document.getElementById("result-cases");
	const compileErrorEl = document.getElementById("compile-error");
	const toHistoryBtn = document.getElementById("btn-to-history");
	const toProblemBtn = document.getElementById("btn-to-problem");

	if (!submissionId) {
		statusEl.innerHTML = '<div class="error-box">无效的提交 ID</div>';
		return null;
	}

	titleEl.textContent = `提交 #${submissionId}`;
	setLoading(summaryEl, "结果加载中...");

	let pollTimer = 0;
	let lastSubmission = null;

	async function refreshResult() {
		try {
			const submission = await getSubmission(submissionId);
			lastSubmission = submission;

			const finalStatus = submission.result?.status || submission.status || "pending";
			const ns = normalizeStatus(finalStatus);
			statusEl.innerHTML = `<span class="status-badge ${ns.className}">${escapeHtml(ns.text)}</span>`;

			const summary = submission.result?.summary || {};
			summaryEl.innerHTML = `
				<div class="toolbar">
					<span class="kv"><strong>提交状态</strong><span>${escapeHtml(submission.status || "-")}</span></span>
					<span class="kv"><strong>总用例</strong><span>${summary.total ?? "-"}</span></span>
					<span class="kv"><strong>通过</strong><span>${summary.passed ?? "-"}</span></span>
					<span class="kv"><strong>总耗时</strong><span>${summary.total_time_ms ?? submission.time_ms ?? "-"} ms</span></span>
					<span class="kv"><strong>峰值内存</strong><span>${summary.peak_memory_kb ?? submission.memory_kb ?? "-"} KB</span></span>
				</div>
			`;

			caseBody.innerHTML = renderCaseRows(submission.result?.results || []);

			const compileError = submission.result?.compile_error;
			compileErrorEl.innerHTML = compileError ? `<div class="error-box">${escapeHtml(compileError)}</div>` : "";

			const done = isFinished(finalStatus);
			loadingEl.style.display = done ? "none" : "inline-flex";

			if (!done && !pollTimer) {
				pollTimer = window.setInterval(refreshResult, 1800);
			}
			if (done && pollTimer) {
				window.clearInterval(pollTimer);
				pollTimer = 0;
			}
		} catch (error) {
			if (pollTimer) {
				window.clearInterval(pollTimer);
				pollTimer = 0;
			}
			showToast(error.message || "加载提交结果失败", "error");
			summaryEl.innerHTML = `<div class="error-box">${escapeHtml(error.message || "请求失败")}</div>`;
		}
	}

	const onToHistory = () => ctx.navigate("/history");
	const onToProblem = () => {
		if (!lastSubmission?.problem_id) {
			showToast("没有题目信息", "error");
			return;
		}
		ctx.navigate(`/problems/${lastSubmission.problem_id}`);
	};

	toHistoryBtn?.addEventListener("click", onToHistory);
	toProblemBtn?.addEventListener("click", onToProblem);

	refreshResult();

	return () => {
		toHistoryBtn?.removeEventListener("click", onToHistory);
		toProblemBtn?.removeEventListener("click", onToProblem);
		if (pollTimer) {
			window.clearInterval(pollTimer);
		}
	};
}
