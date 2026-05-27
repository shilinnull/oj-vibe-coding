import { login, register } from "../api.js";
import { getCurrentUser, setAuth, showToast } from "../utils.js";

function parseAuthError(error, mode) {
	const status = Number(error?.status || 0);
	const payloadError = String(error?.payload?.error || "").toLowerCase();
	const message = String(error?.payload?.message || error?.message || "").trim();

	if (status === 401 || payloadError.includes("invalid credentials")) {
		return "用户名或密码错误";
	}
	if (status === 409 || payloadError.includes("user exists")) {
		return "用户名已存在";
	}
	if (status === 400) {
		if (payloadError.includes("username and password required")) {
			return mode === "register" ? "用户名和密码必填" : "请输入用户名和密码";
		}
		return message || "请求参数不正确";
	}
	if (status === 500 || payloadError.includes("internal")) {
		return "服务器发生错误，请稍后重试";
	}
	return message || "请求失败，请稍后重试";
}

export function initLoginPage(ctx) {
	const user = getCurrentUser();
	if (user.isAuthed) {
		ctx.navigate("/problems");
		return null;
	}

	const form = document.getElementById("login-form");
	const submitBtn = document.getElementById("login-submit");

	const onSubmit = async (event) => {
		event.preventDefault();
		const formData = new FormData(form);
		const username = String(formData.get("username") || "").trim();
		const password = String(formData.get("password") || "");

		if (!username || !password) {
			showToast("请输入用户名和密码", "error");
			return;
		}

		submitBtn.disabled = true;
		submitBtn.textContent = "登录中...";
		try {
			const result = await login(username, password);
			setAuth(result.token, result.id, username);
			ctx.refreshUser();
			showToast("登录成功", "success");
			ctx.navigate("/problems");
		} catch (error) {
			showToast(parseAuthError(error, "login"), "error");
		} finally {
			submitBtn.disabled = false;
			submitBtn.textContent = "登录";
		}
	};

	form?.addEventListener("submit", onSubmit);

	return () => {
		form?.removeEventListener("submit", onSubmit);
	};
}

export function initRegisterPage(ctx) {
	const form = document.getElementById("register-form");
	const submitBtn = document.getElementById("register-submit");

	const onSubmit = async (event) => {
		event.preventDefault();
		const formData = new FormData(form);
		const username = String(formData.get("username") || "").trim();
		const password = String(formData.get("password") || "");
		const email = String(formData.get("email") || "").trim();

		if (!username || !password) {
			showToast("用户名和密码必填", "error");
			return;
		}
		if (password.length < 6) {
			showToast("密码至少 6 位", "error");
			return;
		}

		submitBtn.disabled = true;
		submitBtn.textContent = "注册中...";
		try {
			const result = await register(username, password, email);
			setAuth(result.token, result.id, username);
			ctx.refreshUser();
			showToast("注册成功", "success");
			ctx.navigate("/problems");
		} catch (error) {
			showToast(parseAuthError(error, "register"), "error");
		} finally {
			submitBtn.disabled = false;
			submitBtn.textContent = "注册并登录";
		}
	};

	form?.addEventListener("submit", onSubmit);

	return () => {
		form?.removeEventListener("submit", onSubmit);
	};
}
