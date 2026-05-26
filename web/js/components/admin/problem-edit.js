import {
	adminAddTestcase,
	adminCreateProblem,
	adminDeleteProblem,
	adminGetProblem,
	adminUpdateProblem,
	adminUploadTestcasesZip,
	getProblems,
} from "../../api.js";
import { escapeHtml, showToast } from "../../utils.js";

function $(id) {
	return document.getElementById(id);
}

function setVisible(el, visible, display = "inline-flex") {
	if (!el) return;
	el.style.display = visible ? display : "none";
}

function getFormValue(id) {
	return String($(id)?.value || "");
}

function setFormValue(id, value) {
	const el = $(id);
	if (el) el.value = value;
}

function getCheckbox(id) {
	return Boolean($(id)?.checked);
}

function setCheckbox(id, checked) {
	const el = $(id);
	if (el) el.checked = Boolean(checked);
}

function setZipProgress(percent, text) {
	const wrap = $("admin-zip-progress-wrap");
	const bar = $("admin-zip-progress");
	const label = $("admin-zip-progress-text");
	if (!wrap || !bar || !label) return;
	bar.value = Math.max(0, Math.min(100, Number(percent) || 0));
	label.textContent = text || `${bar.value}%`;
	wrap.style.display = "block";
}

function hideZipProgress() {
	const wrap = $("admin-zip-progress-wrap");
	const bar = $("admin-zip-progress");
	const label = $("admin-zip-progress-text");
	if (wrap) wrap.style.display = "none";
	if (bar) bar.value = 0;
	if (label) label.textContent = "0%";
}

function fillProblemList(items) {
	const sel = $("admin-problem-select");
	if (!sel) return;
	sel.innerHTML = `<option value="">（新建题目）</option>`;
	(items || []).forEach((p) => {
		const opt = document.createElement("option");
		opt.value = String(p.id);
		opt.textContent = `#${p.id} ${p.title || ""}`;
		sel.appendChild(opt);
	});
}

function renderTestcases(items) {
	const tbody = $("admin-testcases-tbody");
	if (!tbody) return;
	if (!Array.isArray(items) || items.length === 0) {
		tbody.innerHTML = '<tr><td colspan="5" class="muted">暂无用例</td></tr>';
		return;
	}
	tbody.innerHTML = items
		.map((tc) => {
			return `
			<tr>
				<td>${escapeHtml(tc.id)}</td>
				<td>${tc.is_sample ? "是" : "否"}</td>
				<td>${escapeHtml(tc.sort_order)}</td>
				<td><pre class="md-block">${escapeHtml(tc.input || "")}</pre></td>
				<td><pre class="md-block">${escapeHtml(tc.output || "")}</pre></td>
			</tr>`;
		})
		.join("");
}

export function initAdminProblemEditPage() {
	let aborted = false;
	let currentProblemId = 0;
	let selectedZipFile = null;

	async function uploadZipFile(file) {
		if (!currentProblemId) {
			showToast("请先选择或创建题目", "error");
			return;
		}
		if (!file) {
			showToast("请选择 zip 文件", "error");
			return;
		}
		try {
			setZipProgress(0, "准备上传...");
			setVisible($("admin-zip-loading"), true);
			await adminUploadTestcasesZip(currentProblemId, file, (percent) => {
				setZipProgress(percent, `上传中 ${percent}%`);
			});
			setZipProgress(100, "处理中...");
			showToast("已导入用例（替换所有非样例）", "success");
			selectedZipFile = null;
			$("admin-zip-file").value = "";
			const filenameEl = $("admin-zip-filename");
			if (filenameEl) filenameEl.textContent = "未选择文件";
			await loadProblem(currentProblemId);
		} catch (e) {
			showToast(e?.message || "上传失败", "error");
		} finally {
			setVisible($("admin-zip-loading"), false);
			hideZipProgress();
		}
	}

	async function loadProblemList(selectId = 0) {
		try {
			setVisible($("admin-problem-loading"), true);
			const payload = await getProblems({ limit: 100, offset: 0, status: "" });
			if (aborted) return;
			fillProblemList(payload?.items || []);
			if (selectId) {
				$("admin-problem-select").value = String(selectId);
			}
		} catch (e) {
			showToast(e?.message || "加载题目失败", "error");
		} finally {
			setVisible($("admin-problem-loading"), false);
		}
	}

	function clearEditor() {
		currentProblemId = 0;
		setFormValue("admin-problem-id", "");
		setFormValue("admin-title", "");
		setFormValue("admin-difficulty", "medium");
		setFormValue("admin-time", "1000");
		setFormValue("admin-memory", "262144");
		setFormValue("admin-status", "draft");
		setFormValue("admin-description", "");
		renderTestcases([]);
	}

	async function loadProblem(problemId) {
		try {
			setVisible($("admin-problem-loading"), true);
			const p = await adminGetProblem(problemId);
			if (aborted) return;
			currentProblemId = Number(p.id) || 0;
			setFormValue("admin-problem-id", String(p.id || ""));
			setFormValue("admin-title", p.title || "");
			setFormValue("admin-difficulty", p.difficulty || "medium");
			setFormValue("admin-time", String(p.time_limit_ms ?? 1000));
			setFormValue("admin-memory", String(p.memory_limit_kb ?? 262144));
			setFormValue("admin-status", p.status || "draft");
			setFormValue("admin-description", p.description || "");
			renderTestcases(p.test_cases || []);
		} catch (e) {
			showToast(e?.message || "加载题目失败", "error");
		} finally {
			setVisible($("admin-problem-loading"), false);
		}
	}

	function collectProblemForm() {
		return {
			title: getFormValue("admin-title").trim(),
			difficulty: getFormValue("admin-difficulty"),
			time_limit_ms: Number(getFormValue("admin-time") || 0) || 1000,
			memory_limit_kb: Number(getFormValue("admin-memory") || 0) || 262144,
			status: getFormValue("admin-status"),
			description: getFormValue("admin-description"),
		};
	}

	$("admin-problem-select")?.addEventListener("change", async (ev) => {
		const id = Number(ev.target.value || 0);
		if (!id) {
			clearEditor();
			return;
		}
		await loadProblem(id);
	});

	$("admin-problem-new")?.addEventListener("click", () => {
		$("admin-problem-select").value = "";
		clearEditor();
	});

	$("admin-problem-save")?.addEventListener("click", async () => {
		const body = collectProblemForm();
		if (!body.title || !body.description) {
			showToast("标题与题面不能为空", "error");
			return;
		}
		try {
			if (currentProblemId) {
				await adminUpdateProblem(currentProblemId, body);
				showToast("已保存", "success");
				await loadProblem(currentProblemId);
			} else {
				const created = await adminCreateProblem(body);
				const newId = Number(created?.id || 0);
				showToast("已创建", "success");
				await loadProblemList(newId);
				if (newId) await loadProblem(newId);
			}
		} catch (e) {
			showToast(e?.message || "保存失败", "error");
		}
	});

	$("admin-problem-delete")?.addEventListener("click", async () => {
		if (!currentProblemId) {
			showToast("请选择要删除的题目", "error");
			return;
		}
		if (!window.confirm(`确认删除题目 #${currentProblemId} ?`)) {
			return;
		}
		try {
			await adminDeleteProblem(currentProblemId);
			showToast("已删除", "success");
			clearEditor();
			await loadProblemList(0);
		} catch (e) {
			showToast(e?.message || "删除失败", "error");
		}
	});

	$("admin-add-tc")?.addEventListener("click", async () => {
		if (!currentProblemId) {
			showToast("请先选择或创建题目", "error");
			return;
		}
		const input = getFormValue("admin-tc-in");
		const output = getFormValue("admin-tc-out");
		const sort_order = Number(getFormValue("admin-tc-order") || 0) || 0;
		const is_sample = getCheckbox("admin-tc-sample");
		if (!input && !output) {
			showToast("请输入用例内容", "error");
			return;
		}
		try {
			await adminAddTestcase(currentProblemId, { input, output, sort_order, is_sample });
			showToast("已添加用例", "success");
			setFormValue("admin-tc-in", "");
			setFormValue("admin-tc-out", "");
			setFormValue("admin-tc-order", "");
			setCheckbox("admin-tc-sample", false);
			await loadProblem(currentProblemId);
		} catch (e) {
			showToast(e?.message || "添加失败", "error");
		}
	});

	$("admin-upload-zip")?.addEventListener("click", () => {
		const file = selectedZipFile || $("admin-zip-file")?.files?.[0];
		if (file) {
			void uploadZipFile(file);
			return;
		}
		showToast("请先选择 zip 文件", "error");
	});

	$("admin-zip-select")?.addEventListener("click", () => {
		$("admin-zip-file")?.click();
	});

	$("admin-zip-file")?.addEventListener("change", async (ev) => {
		const file = ev.target.files?.[0];
		if (!file) {
			selectedZipFile = null;
			const filenameEl = $("admin-zip-filename");
			if (filenameEl) filenameEl.textContent = "未选择文件";
			return;
		}
		selectedZipFile = file;
		const filenameEl = $("admin-zip-filename");
		if (filenameEl) filenameEl.textContent = file.name || "已选择文件";
	});

	clearEditor();
	loadProblemList();

	return () => {
		aborted = true;
	};
}
