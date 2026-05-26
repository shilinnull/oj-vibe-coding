import { adminCreateLanguage, adminListLanguages, adminUpdateLanguage } from "../../api.js";
import { escapeHtml, showToast } from "../../utils.js";

function $(id) {
	return document.getElementById(id);
}

function setVisible(el, visible, display = "inline-flex") {
	if (!el) return;
	el.style.display = visible ? display : "none";
}

function getVal(id) {
	return String($(id)?.value || "");
}

function setVal(id, v) {
	const el = $(id);
	if (el) el.value = v;
}

function getChecked(id) {
	return Boolean($(id)?.checked);
}

function setChecked(id, v) {
	const el = $(id);
	if (el) el.checked = Boolean(v);
}

function renderTable(items) {
	const tbody = $("admin-lang-tbody");
	if (!tbody) return;
	if (!Array.isArray(items) || items.length === 0) {
		tbody.innerHTML = '<tr><td colspan="6" class="muted">暂无语言</td></tr>';
		return;
	}
	tbody.innerHTML = items
		.map((l) => {
			return `
			<tr data-lang-id="${escapeHtml(l.id)}">
				<td>${escapeHtml(l.id)}</td>
				<td>${escapeHtml(l.name)}</td>
				<td>${escapeHtml(l.extension)}</td>
				<td>${l.enabled ? "启用" : "禁用"}</td>
				<td><button class="btn btn-ghost" data-edit-lang="${escapeHtml(l.id)}">编辑</button></td>
				<td class="muted">修改后会影响提交语言下拉</td>
			</tr>`;
		})
		.join("");
}

export function initAdminLanguageConfigPage() {
	let aborted = false;
	let currentLangId = 0;
	let lastItems = [];

	function clearForm() {
		currentLangId = 0;
		setVal("admin-lang-id", "");
		setVal("admin-lang-name", "");
		setVal("admin-lang-ext", "");
		setVal("admin-lang-compile", "");
		setVal("admin-lang-run", "");
		setChecked("admin-lang-enabled", true);
	}

	async function load() {
		try {
			setVisible($("admin-lang-loading"), true);
			const payload = await adminListLanguages();
			if (aborted) return;
			lastItems = Array.isArray(payload?.items) ? payload.items : [];
			renderTable(lastItems);
		} catch (e) {
			showToast(e?.message || "加载失败", "error");
		} finally {
			setVisible($("admin-lang-loading"), false);
		}
	}

	function fillForm(lang) {
		currentLangId = Number(lang.id) || 0;
		setVal("admin-lang-id", String(lang.id || ""));
		setVal("admin-lang-name", lang.name || "");
		setVal("admin-lang-ext", lang.extension || "");
		setVal("admin-lang-compile", lang.compile_cmd || "");
		setVal("admin-lang-run", lang.run_cmd || "");
		setChecked("admin-lang-enabled", Boolean(lang.enabled));
	}

	$("admin-lang-refresh")?.addEventListener("click", load);
	$("admin-lang-new")?.addEventListener("click", clearForm);

	$("admin-lang-save")?.addEventListener("click", async () => {
		const body = {
			name: getVal("admin-lang-name").trim(),
			extension: getVal("admin-lang-ext").trim(),
			compile_cmd: getVal("admin-lang-compile"),
			run_cmd: getVal("admin-lang-run"),
			enabled: getChecked("admin-lang-enabled"),
		};
		if (!body.name || !body.extension || !body.compile_cmd || !body.run_cmd) {
			showToast("请完整填写字段", "error");
			return;
		}
		try {
			if (currentLangId) {
				await adminUpdateLanguage(currentLangId, body);
				showToast("已保存", "success");
			} else {
				await adminCreateLanguage(body);
				showToast("已创建", "success");
			}
			await load();
			clearForm();
		} catch (e) {
			showToast(e?.message || "保存失败", "error");
		}
	});

	document.body.addEventListener("click", (ev) => {
		const btn = ev.target.closest("[data-edit-lang]");
		if (!btn) return;
		const id = Number(btn.getAttribute("data-edit-lang") || 0);
		const lang = lastItems.find((x) => Number(x.id) === id);
		if (lang) fillForm(lang);
	});

	clearForm();
	load();

	return () => {
		aborted = true;
	};
}
