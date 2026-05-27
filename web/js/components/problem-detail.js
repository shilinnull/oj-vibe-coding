import { createSubmission, getLanguageOptions, getProblem } from "../api.js";
import { createCodeEditor } from "./editor.js";
import { escapeHtml, getCurrentUser, setLoading, showToast } from "../utils.js";

function renderSamples(container, samples) {
	const list = Array.isArray(samples) ? samples : [];
	if (!list.length) {
		container.innerHTML = '<p class="muted">暂无样例。</p>';
		return;
	}

	container.innerHTML = `
		<div class="table-wrap">
			<table>
				<thead>
					<tr>
						<th>ID</th>
						<th>输入</th>
						<th>输出</th>
					</tr>
				</thead>
				<tbody>
					${list
						.map(
							(s) => `
						<tr>
							<td>${s.id ?? "-"}</td>
							<td>${escapeHtml(s.input ?? "")}</td>
							<td>${escapeHtml(s.output ?? "")}</td>
						</tr>
					`,
						)
						.join("")}
				</tbody>
			</table>
		</div>
	`;
}

function getStarterCode(monacoLang) {
	if (monacoLang === "c") {
		return [
			"#include <stdio.h>",
			"",
			"int main() {",
			"  // TODO: write your solution",
			"  return 0;",
			"}",
			"",
		].join("\n");
	}
	return [
		"#include <bits/stdc++.h>",
		"using namespace std;",
		"",
		"int main() {",
		"  ios::sync_with_stdio(false);",
		"  cin.tie(nullptr);",
		"  // TODO: write your solution",
		"  return 0;",
		"}",
		"",
	].join("\n");
}

function getDraftKey(problemId, languageId) {
	return `oj_problem_draft_${problemId}_${languageId}`;
}

function getDraftLanguageKey(problemId) {
	return `oj_problem_draft_lang_${problemId}`;
}

function loadDraft(problemId, languageId) {
	return localStorage.getItem(getDraftKey(problemId, languageId)) || "";
}

function saveDraft(problemId, languageId, sourceCode) {
	localStorage.setItem(getDraftKey(problemId, languageId), sourceCode || "");
	localStorage.setItem(getDraftLanguageKey(problemId), String(languageId || 0));
}

export function initProblemDetailPage(ctx) {
	const id = Number(ctx.route.params.id || 0);
	const titleEl = document.getElementById("problem-title");
	const descEl = document.getElementById("problem-description");
	const metaEl = document.getElementById("problem-meta");
	const sampleEl = document.getElementById("problem-samples");
	const editorHost = document.getElementById("editor-host");
	const runBtn = document.getElementById("btn-run");
	const submitBtn = document.getElementById("btn-submit");
	const langSelect = document.getElementById("language-select");
	const backListBtn = document.getElementById("btn-back-list");

	let editor = null;
	let languageOptions = [];
	let activeLanguage = { id: 1, monacoLang: "cpp" };
	let restoringEditor = false;

	if (!id) {
		titleEl.textContent = "题目不存在";
		return null;
	}

	async function setupEditor() {
		languageOptions = await getLanguageOptions();
		langSelect.innerHTML = languageOptions
			.map((item) => `<option value="${item.id}">${escapeHtml(item.name)}</option>`)
			.join("");
		activeLanguage = languageOptions[0] || activeLanguage;
		const savedLanguageId = Number(localStorage.getItem(getDraftLanguageKey(id)) || 0);
		activeLanguage = languageOptions.find((item) => item.id === savedLanguageId) || languageOptions[0] || activeLanguage;
		langSelect.value = String(activeLanguage.id || 1);
		const savedCode = loadDraft(id, activeLanguage.id);
		editor = await createCodeEditor(editorHost, {
			language: activeLanguage.monacoLang,
			value: savedCode || getStarterCode(activeLanguage.monacoLang),
			onChange: (sourceCode) => {
				if (restoringEditor) {
					return;
				}
				saveDraft(id, activeLanguage.id, sourceCode);
			},
		});
	}

	async function loadProblem() {
		setLoading(descEl, "题目加载中...");
		try {
			const problem = await getProblem(id);
			titleEl.textContent = `#${problem.id} ${problem.title}`;
			descEl.innerHTML = `<div class="md-block">${escapeHtml(problem.description || "")}</div>`;
			metaEl.innerHTML = `
				<span class="kv"><strong>难度</strong><span>${escapeHtml(problem.difficulty || "-")}</span></span>
				<span class="kv"><strong>时间限制</strong><span>${problem.time_limit_ms || "-"} ms</span></span>
				<span class="kv"><strong>内存限制</strong><span>${problem.memory_limit_kb || "-"} KB</span></span>
			`;
			renderSamples(sampleEl, problem.samples);
		} catch (error) {
			descEl.innerHTML = `<div class="error-box">${escapeHtml(error.message)}</div>`;
		}
	}

	async function submit(mode) {
		const user = getCurrentUser();
		if (!user.userId) {
			showToast("请先登录", "error");
			ctx.navigate("/login");
			return;
		}

		const sourceCode = editor?.getValue?.() || "";
		if (!sourceCode.trim()) {
			showToast("代码不能为空", "error");
			return;
		}
		saveDraft(id, activeLanguage.id, sourceCode);

		runBtn.disabled = true;
		submitBtn.disabled = true;

		try {
			const result = await createSubmission({
				source_code: sourceCode,
				problem_id: id,
				language_id: activeLanguage.id,
				user_id: user.userId,
				mode,
			});
			showToast(mode === "run" ? "样例运行已开始" : "提交成功，正在判题", "success");
			ctx.navigate(`/submissions/${result.id}`);
		} catch (error) {
			showToast(error.message || "提交失败", "error");
		} finally {
			runBtn.disabled = false;
			submitBtn.disabled = false;
		}
	}

	const onLanguageChange = () => {
		const chosenId = Number(langSelect.value || 1);
		const found = languageOptions.find((item) => item.id === chosenId);
		if (!found) {
			return;
		}
		const previousLanguageId = activeLanguage.id;
		const currentCode = editor?.getValue?.() || "";
		if (previousLanguageId) {
			saveDraft(id, previousLanguageId, currentCode);
		}
		activeLanguage = found;
		const nextCode = loadDraft(id, found.id) || getStarterCode(found.monacoLang);
		restoringEditor = true;
		editor?.setLanguage(found.monacoLang);
		editor?.setValue(nextCode);
		restoringEditor = false;
	};

	const onRun = () => submit("run");
	const onSubmit = () => submit("submit");
	const onBackList = () => ctx.navigate("/problems");

	langSelect?.addEventListener("change", onLanguageChange);
	runBtn?.addEventListener("click", onRun);
	submitBtn?.addEventListener("click", onSubmit);
	backListBtn?.addEventListener("click", onBackList);

	loadProblem();
	setupEditor();

	return () => {
		langSelect?.removeEventListener("change", onLanguageChange);
		runBtn?.removeEventListener("click", onRun);
		submitBtn?.removeEventListener("click", onSubmit);
		backListBtn?.removeEventListener("click", onBackList);
		editor?.dispose?.();
	};
}
