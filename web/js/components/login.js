import { login, register } from "../api.js";
import { getCurrentUser, setAuth, showToast } from "../utils.js";

function parseLoginError(error) {
	return error?.message || "请求失败，请稍后重试";
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
			showToast(parseLoginError(error), "error");
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
			showToast(parseLoginError(error), "error");
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
