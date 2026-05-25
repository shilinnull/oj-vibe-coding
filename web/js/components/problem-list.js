import { getProblems } from "../api.js";
import { debounce, escapeHtml, setLoading, showToast } from "../utils.js";

function difficultyTag(level) {
	const value = String(level || "medium").toLowerCase();
	if (value === "easy") {
		return '<span class="tag tag-easy">Easy</span>';
	}
	if (value === "hard") {
		return '<span class="tag tag-hard">Hard</span>';
	}
	return '<span class="tag tag-medium">Medium</span>';
}

export function initProblemListPage(ctx) {
	const listEl = document.getElementById("problem-list");
	const countEl = document.getElementById("problem-count");
	const pagerInfoEl = document.getElementById("problem-pager-info");
	const searchInput = document.getElementById("problem-search");
	const difficultySelect = document.getElementById("problem-difficulty");
	const prevBtn = document.getElementById("problem-prev");
	const nextBtn = document.getElementById("problem-next");

	let limit = 12;
	let offset = 0;
	let remoteItems = [];
	let filteredItems = [];

	async function fetchPage() {
		setLoading(listEl, "题目加载中...");
		prevBtn.disabled = true;
		nextBtn.disabled = true;
		try {
			const data = await getProblems({ limit, offset, status: "published" });
			remoteItems = Array.isArray(data.items) ? data.items : [];
			applyFilter();
			pagerInfoEl.textContent = `第 ${Math.floor(offset / limit) + 1} 页`;
			prevBtn.disabled = offset <= 0;
			nextBtn.disabled = remoteItems.length < limit;
		} catch (error) {
			listEl.innerHTML = `<div class="error-box">${escapeHtml(error.message)}</div>`;
			showToast(error.message || "题目获取失败", "error");
		}
	}

	function applyFilter() {
		const kw = String(searchInput.value || "").trim().toLowerCase();
		const level = String(difficultySelect.value || "").toLowerCase();
		filteredItems = remoteItems.filter((item) => {
			const hitKeyword = !kw || String(item.title || "").toLowerCase().includes(kw);
			const hitDifficulty = !level || String(item.difficulty || "").toLowerCase() === level;
			return hitKeyword && hitDifficulty;
		});
		renderList();
	}

	function renderList() {
		countEl.textContent = String(filteredItems.length);
		if (!filteredItems.length) {
			listEl.innerHTML = '<div class="card"><p class="muted">没有匹配的题目。</p></div>';
			return;
		}
		listEl.innerHTML = filteredItems
			.map(
				(item) => `
				<a class="card problem-card" href="#/problems/${item.id}" data-link>
					<div class="page-head">
						<h3 class="problem-title">#${item.id} ${escapeHtml(item.title)}</h3>
						${difficultyTag(item.difficulty)}
					</div>
					<div class="toolbar muted">
						<span>时间限制: ${item.time_limit_ms || "-"} ms</span>
						<span>内存限制: ${item.memory_limit_kb || "-"} KB</span>
						<span>状态: ${escapeHtml(item.status || "-")}</span>
					</div>
				</a>
			`,
			)
			.join("");
	}

	const onSearchInput = debounce(applyFilter, 250);
	const onDifficulty = () => applyFilter();
	const onPrev = () => {
		offset = Math.max(0, offset - limit);
		fetchPage();
	};
	const onNext = () => {
		offset += limit;
		fetchPage();
	};

	searchInput?.addEventListener("input", onSearchInput);
	difficultySelect?.addEventListener("change", onDifficulty);
	prevBtn?.addEventListener("click", onPrev);
	nextBtn?.addEventListener("click", onNext);

	fetchPage();

	return () => {
		searchInput?.removeEventListener("input", onSearchInput);
		difficultySelect?.removeEventListener("change", onDifficulty);
		prevBtn?.removeEventListener("click", onPrev);
		nextBtn?.removeEventListener("click", onNext);
	};
}
