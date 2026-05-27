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
	const MAX_CHARS = 600;
	const MAX_LINES = 12;

	function makePreview(text) {
		if (text == null) return "";
		const asStr = String(text);
		const lines = asStr.split(/\r?\n/);
		if (asStr.length <= MAX_CHARS && lines.length <= MAX_LINES) {
			return `<pre class=\"output full\">${escapeHtml(asStr)}</pre>`;
		}
		// create truncated preview (preserve lines)
		let preview = lines.slice(0, MAX_LINES).join("\n");
		if (preview.length > MAX_CHARS) preview = preview.slice(0, MAX_CHARS);
		preview = preview.replace(/\n/g, "\n");
		const id = `out-${Math.random().toString(36).slice(2, 9)}`;
		return `
			<div class=\"output-container\">
				<pre id=\"${id}\" class=\"output preview collapsed\">${escapeHtml(preview)}\n…</pre>
				<pre id=\"${id}-full\" class=\"output full\" style=\"display:none\">${escapeHtml(asStr)}</pre>
				<button data-toggle-for=\"${id}\" class=\"btn btn-ghost btn-sm output-toggle\">展开</button>
			</div>
		`;
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
				<td>${makePreview(item.expected_output ?? "")}</td>
				<td>${makePreview(item.actual_output ?? "")}</td>
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
	// 客户端最长等待时长（毫秒），超过后若仍未完成则提示超时（客户端级别）
	const CLIENT_MAX_POLL_MS = 30 * 1000; // 30s
	let pollStart = 0;

	async function refreshResult() {
		try {
			if (!pollStart) pollStart = Date.now();
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

			// attach toggle handlers via event delegation
			// (we re-render table body each refresh, so use delegation on caseBody)
			// no-op if already attached because we remove on cleanup

			const compileError = submission.result?.compile_error;
			compileErrorEl.innerHTML = compileError ? `<div class="error-box">${escapeHtml(compileError)}</div>` : "";

			const done = isFinished(finalStatus);
			loadingEl.style.display = done ? "none" : "inline-flex";

			// If not done, start polling (if not already). Also enforce a client-side max wait.
			if (!done) {
				if (!pollTimer) {
					pollTimer = window.setInterval(refreshResult, 1800);
				}
				const elapsed = Date.now() - pollStart;
				if (elapsed >= CLIENT_MAX_POLL_MS) {
					// stop polling and show timeout to user
					if (pollTimer) {
						window.clearInterval(pollTimer);
						pollTimer = 0;
					}
					const nsTLE = normalizeStatus("time_limit_exceeded");
					statusEl.innerHTML = `<span class="status-badge ${nsTLE.className}">${escapeHtml(nsTLE.text)}</span>`;
					  // 仅显示 TIME_LIMIT_EXCEEDED：设置状态并把每个用例标为 TLE
					  try {
						  const caseCount = submission.result?.sample_case_count || summary.total || 1;
						  const fakeResults = [];
						  for (let i = 1; i <= caseCount; ++i) {
							  fakeResults.push({
								  test_case_id: i,
								  status: "time_limit_exceeded",
								  time_ms: null,
								  memory_kb: null,
								  expected_output: "",
								  actual_output: "",
							  });
						  }
						  caseBody.innerHTML = renderCaseRows(fakeResults);
					  } catch (e) {
						  // ignore
					  }
					  loadingEl.style.display = "none";
					  return;
				}
			} else {
				// finished
				if (pollTimer) {
					window.clearInterval(pollTimer);
					pollTimer = 0;
				}
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

	// toggle handler for expand/collapse of long outputs
	function onToggleClick(e) {
		const btn = e.target.closest?.("button.output-toggle");
		if (!btn) return;
		const id = btn.getAttribute("data-toggle-for");
		if (!id) return;
		const preview = document.getElementById(id);
		const full = document.getElementById(id + "-full");
		if (!preview || !full) return;
		const expanded = preview.style.display === "none";
		if (expanded) {
			preview.style.display = "";
			full.style.display = "none";
			btn.textContent = "展开";
		} else {
			preview.style.display = "none";
			full.style.display = "";
			btn.textContent = "折叠";
		}
	}

	caseBody?.addEventListener("click", onToggleClick);

	refreshResult();

	return () => {
		toHistoryBtn?.removeEventListener("click", onToHistory);
		toProblemBtn?.removeEventListener("click", onToProblem);
		caseBody?.removeEventListener("click", onToggleClick);
		if (pollTimer) {
			window.clearInterval(pollTimer);
		}
	};
}
