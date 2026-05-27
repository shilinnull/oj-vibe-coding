let monacoLoadingPromise = null;

function loadScript(src) {
	return new Promise((resolve, reject) => {
		const existing = document.querySelector(`script[data-monaco-src="${src}"]`);
		if (existing) {
			existing.addEventListener("load", () => resolve(), { once: true });
			existing.addEventListener("error", () => reject(new Error(`load failed: ${src}`)), { once: true });
			return;
		}

		const script = document.createElement("script");
		script.src = src;
		script.async = true;
		script.dataset.monacoSrc = src;
		script.onload = () => resolve();
		script.onerror = () => reject(new Error(`load failed: ${src}`));
		document.head.appendChild(script);
	});
}

async function initMonacoFromBase(basePath) {
	if (window.monaco?.editor) {
		return true;
	}
	await loadScript(`${basePath}/loader.js`);
	const req = window.require;
	if (!req) {
		return false;
	}
	return new Promise((resolve) => {
		req.config({ paths: { vs: basePath } });
		req(["vs/editor/editor.main"], () => resolve(true), () => resolve(false));
	});
}

async function ensureMonaco() {
	if (window.monaco?.editor) {
		return true;
	}
	if (!monacoLoadingPromise) {
		monacoLoadingPromise = (async () => {
			const localOk = await initMonacoFromBase("./lib/monaco/min/vs").catch(() => false);
			if (localOk) {
				return true;
			}
			const cdnOk = await initMonacoFromBase("https://unpkg.com/monaco-editor@0.52.2/min/vs").catch(() => false);
			return cdnOk;
		})();
	}
	return monacoLoadingPromise;
}

function createFallbackEditor(container, value) {
	const textarea = document.createElement("textarea");
	textarea.className = "code-fallback";
	textarea.value = value || "";
	container.innerHTML = "";
	container.appendChild(textarea);
	return {
		getValue: () => textarea.value,
		setValue: (next) => {
			textarea.value = next;
		},
		setLanguage: () => {},
		dispose: () => {},
	};
}

export async function createCodeEditor(container, options = {}) {
	const initialValue = options.value || "";
	const lang = options.language || "cpp";
	const onChange = typeof options.onChange === "function" ? options.onChange : null;

	const ready = await ensureMonaco();
	if (!ready || !window.monaco?.editor) {
		const fallback = createFallbackEditor(container, initialValue);
		if (onChange) {
			const textarea = container.querySelector("textarea.code-fallback");
			if (textarea) {
				textarea.addEventListener("input", () => onChange(fallback.getValue()));
			}
			const originalSetValue = fallback.setValue;
			fallback.setValue = (next) => {
				originalSetValue(next);
				onChange(fallback.getValue());
			};
		}
		return fallback;
	}

	const editor = window.monaco.editor.create(container, {
		value: initialValue,
		language: lang,
		theme: "vs-dark",
		minimap: { enabled: false },
		fontSize: 14,
		automaticLayout: true,
		wordWrap: "on",
		tabSize: 2,
		scrollBeyondLastLine: false,
	});

	const changeListener = onChange
		? editor.onDidChangeModelContent(() => {
			const model = editor.getModel();
			onChange(model ? model.getValue() : editor.getValue());
		})
		: null;

	return {
		getValue: () => editor.getValue(),
		setValue: (next) => editor.setValue(next || ""),
		setLanguage: (nextLang) => {
			const model = editor.getModel();
			if (model) {
				window.monaco.editor.setModelLanguage(model, nextLang || "cpp");
			}
		},
		dispose: () => {
			changeListener?.dispose?.();
			editor.dispose();
		},
	};
}
