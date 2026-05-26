#!/usr/bin/env python3
"""SPEC-driven API smoke test for OJ server.

按 SPEC.md 的“API 端点设计”分组执行：
- 认证模块
- 题目模块
- 提交模块
- 管理后台

Usage:
  python3 tests/test_api.py --base-url http://127.0.0.1:8080
"""

from __future__ import annotations

import argparse
import io
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass
from typing import Any, Optional


@dataclass
class HttpResult:
    status: int
    body: str


class ApiSmokeTester:
    def __init__(self, base_url: str, timeout: float = 8.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor())
        self.pass_count = 0
        self.fail_count = 0

    def _request(
        self,
        method: str,
        path: str,
        payload: Optional[dict[str, Any]] = None,
        headers: Optional[dict[str, str]] = None,
        raw_body: Optional[bytes] = None,
        content_type: Optional[str] = None,
    ) -> HttpResult:
        url = f"{self.base_url}{path}"
        req_headers = dict(headers or {})

        data: Optional[bytes] = None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            req_headers["Content-Type"] = "application/json"
        elif raw_body is not None:
            data = raw_body
            if content_type:
                req_headers["Content-Type"] = content_type

        req = urllib.request.Request(url=url, data=data, headers=req_headers, method=method)
        try:
            with self.opener.open(req, timeout=self.timeout) as resp:
                body = resp.read().decode("utf-8", errors="replace")
                return HttpResult(status=resp.getcode(), body=body)
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")
            return HttpResult(status=e.code, body=body)
        except Exception as e:  # noqa: BLE001
            return HttpResult(status=0, body=str(e))

    @staticmethod
    def _parse_json(text: str) -> Optional[Any]:
        try:
            return json.loads(text)
        except json.JSONDecodeError:
            return None

    def _check(self, condition: bool, title: str, detail: str = "") -> None:
        if condition:
            self.pass_count += 1
            print(f"[PASS] {title}")
        else:
            self.fail_count += 1
            print(f"[FAIL] {title}")
            if detail:
                print(f"       {detail}")

    @staticmethod
    def _zip_testcases() -> bytes:
        # 生成 1.in/1.out 配对，满足 /testcases/upload 的解析规则。
        mem = io.BytesIO()
        with zipfile.ZipFile(mem, mode="w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("1.in", "1 2\n")
            zf.writestr("1.out", "3\n")
        return mem.getvalue()

    def run(
        self,
        problem_id: int,
        seed_user: str,
        seed_pass: str,
        admin_user: str,
        admin_pass: str,
    ) -> int:
        suffix = int(time.time())
        fresh_user = f"api_smoke_{suffix}"
        fresh_pass = "pass1234"
        fresh_email = f"{fresh_user}@example.com"

        print("\n=== 0) health ===")
        health = self._request("GET", "/healthz")
        self._check(health.status == 200 and health.body.strip() == "ok", "GET /healthz", f"status={health.status}, body={health.body}")

        print("\n=== 1) auth endpoints ===")
        reg = self._request("POST", "/api/auth/register", {"username": fresh_user, "password": fresh_pass, "email": fresh_email})
        reg_json = self._parse_json(reg.body)
        new_user_id = int(reg_json.get("id", 0)) if isinstance(reg_json, dict) else 0
        self._check(
            reg.status == 200 and isinstance(reg_json, dict) and bool(reg_json.get("token")) and new_user_id > 0,
            "POST /api/auth/register",
            f"status={reg.status}, body={reg.body}",
        )

        login_new = self._request("POST", "/api/auth/login", {"username": fresh_user, "password": fresh_pass})
        login_new_json = self._parse_json(login_new.body)
        self._check(
            login_new.status == 200 and isinstance(login_new_json, dict) and bool(login_new_json.get("token")),
            "POST /api/auth/login (new user)",
            f"status={login_new.status}, body={login_new.body}",
        )

        login_seed = self._request("POST", "/api/auth/login", {"username": seed_user, "password": seed_pass})
        login_seed_json = self._parse_json(login_seed.body)
        seed_user_id = int(login_seed_json.get("id", 0)) if isinstance(login_seed_json, dict) else 0
        self._check(
            (
                login_seed.status == 200
                and isinstance(login_seed_json, dict)
                and bool(login_seed_json.get("token"))
                and seed_user_id > 0
            )
            or login_seed.status == 401,
            "POST /api/auth/login (seed user)",
            f"status={login_seed.status}, body={login_seed.body}",
        )
        submit_user_id = seed_user_id if seed_user_id > 0 else new_user_id

        logout = self._request("POST", "/api/auth/logout", {})
        logout_json = self._parse_json(logout.body)
        self._check(
            logout.status == 200 and isinstance(logout_json, dict) and logout_json.get("ok") is True,
            "POST /api/auth/logout",
            f"status={logout.status}, body={logout.body}",
        )

        print("\n=== 2) problem endpoints ===")
        plist = self._request("GET", "/api/problems?status=published&limit=20&offset=0")
        plist_json = self._parse_json(plist.body)
        self._check(
            plist.status == 200 and isinstance(plist_json, dict) and isinstance(plist_json.get("items"), list),
            "GET /api/problems",
            f"status={plist.status}, body={plist.body}",
        )

        pdetail = self._request("GET", f"/api/problems/{problem_id}")
        pdetail_json = self._parse_json(pdetail.body)
        self._check(
            pdetail.status == 200 and isinstance(pdetail_json, dict) and int(pdetail_json.get("id", 0)) == problem_id,
            f"GET /api/problems/{problem_id}",
            f"status={pdetail.status}, body={pdetail.body}",
        )

        print("\n=== 3) submission endpoints ===")
        langs = self._request("GET", "/api/languages")
        langs_json = self._parse_json(langs.body)
        lang_id = 0
        if isinstance(langs_json, dict) and isinstance(langs_json.get("items"), list) and langs_json["items"]:
            lang_id = int(langs_json["items"][0].get("id", 0))
        self._check(langs.status == 200 and lang_id > 0, "GET /api/languages", f"status={langs.status}, body={langs.body}")

        sub_create = self._request(
            "POST",
            "/api/submissions",
            {
                "user_id": submit_user_id,
                "problem_id": problem_id,
                "language_id": lang_id,
                "source_code": "#include <iostream>\nint main(){std::cout<<\"3\\n\";return 0;}\n",
                "mode": "run",
            },
        )
        sub_create_json = self._parse_json(sub_create.body)
        submission_id = int(sub_create_json.get("id", 0)) if isinstance(sub_create_json, dict) else 0
        self._check(
            sub_create.status == 201 and isinstance(sub_create_json, dict) and submission_id > 0,
            "POST /api/submissions",
            f"status={sub_create.status}, body={sub_create.body}",
        )

        sub_get = self._request("GET", f"/api/submissions/{submission_id}")
        sub_get_json = self._parse_json(sub_get.body)
        self._check(
            sub_get.status == 200 and isinstance(sub_get_json, dict) and int(sub_get_json.get("id", 0)) == submission_id,
            "GET /api/submissions/{id}",
            f"status={sub_get.status}, body={sub_get.body}",
        )

        sub_list_path = "/api/submissions?" + urllib.parse.urlencode({"user_id": submit_user_id, "limit": 20, "offset": 0})
        sub_list = self._request("GET", sub_list_path)
        sub_list_json = self._parse_json(sub_list.body)
        self._check(
            sub_list.status == 200 and isinstance(sub_list_json, dict) and isinstance(sub_list_json.get("items"), list),
            "GET /api/submissions?user_id=x",
            f"status={sub_list.status}, body={sub_list.body}",
        )

        print("\n=== 4) admin endpoints ===")
        login_admin = self._request("POST", "/api/auth/login", {"username": admin_user, "password": admin_pass})
        login_admin_json = self._parse_json(login_admin.body)
        admin_token = login_admin_json.get("token") if isinstance(login_admin_json, dict) else ""
        admin_headers = {"Authorization": f"Bearer {admin_token}"} if admin_token else {}
        self._check(
            login_admin.status == 200 and isinstance(login_admin_json, dict) and bool(admin_token),
            "POST /api/auth/login (admin)",
            f"status={login_admin.status}, body={login_admin.body}",
        )

        admin_problem = self._request(
            "POST",
            "/api/admin/problems",
            {
                "title": f"API Smoke Problem {suffix}",
                "description": "created by tests/test_api.py",
                "difficulty": "easy",
                "time_limit_ms": 1000,
                "memory_limit_kb": 262144,
                "status": "draft",
            },
            headers=admin_headers,
        )
        admin_problem_json = self._parse_json(admin_problem.body)
        admin_problem_id = int(admin_problem_json.get("id", 0)) if isinstance(admin_problem_json, dict) else 0
        self._check(
            admin_problem.status == 201 and admin_problem_id > 0,
            "POST /api/admin/problems",
            f"status={admin_problem.status}, body={admin_problem.body}",
        )

        get_admin_problem = self._request("GET", f"/api/admin/problems/{admin_problem_id}", headers=admin_headers)
        get_admin_problem_json = self._parse_json(get_admin_problem.body)
        self._check(
            get_admin_problem.status == 200 and isinstance(get_admin_problem_json, dict) and int(get_admin_problem_json.get("id", 0)) == admin_problem_id,
            "GET /api/admin/problems/{id}",
            f"status={get_admin_problem.status}, body={get_admin_problem.body}",
        )

        put_admin_problem = self._request(
            "PUT",
            f"/api/admin/problems/{admin_problem_id}",
            {
                "title": f"API Smoke Problem {suffix} Updated",
                "description": "updated by tests/test_api.py",
                "difficulty": "medium",
                "time_limit_ms": 1500,
                "memory_limit_kb": 262144,
                "status": "published",
            },
            headers=admin_headers,
        )
        put_admin_problem_json = self._parse_json(put_admin_problem.body)
        self._check(
            put_admin_problem.status == 200 and isinstance(put_admin_problem_json, dict) and put_admin_problem_json.get("ok") is True,
            "PUT /api/admin/problems/{id}",
            f"status={put_admin_problem.status}, body={put_admin_problem.body}",
        )

        post_testcase = self._request(
            "POST",
            f"/api/admin/problems/{admin_problem_id}/testcases",
            {"is_sample": True, "input": "1 2\n", "output": "3\n", "sort_order": 1},
            headers=admin_headers,
        )
        post_testcase_json = self._parse_json(post_testcase.body)
        self._check(
            post_testcase.status == 201 and isinstance(post_testcase_json, dict) and int(post_testcase_json.get("id", 0)) > 0,
            "POST /api/admin/problems/{id}/testcases",
            f"status={post_testcase.status}, body={post_testcase.body}",
        )

        upload_zip = self._request(
            "POST",
            f"/api/admin/problems/{admin_problem_id}/testcases/upload",
            headers=admin_headers,
            raw_body=self._zip_testcases(),
            content_type="application/zip",
        )
        upload_zip_json = self._parse_json(upload_zip.body)
        self._check(
            upload_zip.status == 200 and isinstance(upload_zip_json, dict) and upload_zip_json.get("ok") is True,
            "POST /api/admin/problems/{id}/testcases/upload",
            f"status={upload_zip.status}, body={upload_zip.body}",
        )

        admin_subs = self._request("GET", "/api/admin/submissions?limit=20&offset=0", headers=admin_headers)
        admin_subs_json = self._parse_json(admin_subs.body)
        self._check(
            admin_subs.status == 200 and isinstance(admin_subs_json, dict) and isinstance(admin_subs_json.get("items"), list),
            "GET /api/admin/submissions",
            f"status={admin_subs.status}, body={admin_subs.body}",
        )

        admin_langs = self._request("GET", "/api/admin/languages", headers=admin_headers)
        admin_langs_json = self._parse_json(admin_langs.body)
        self._check(
            admin_langs.status == 200 and isinstance(admin_langs_json, dict) and isinstance(admin_langs_json.get("items"), list),
            "GET /api/admin/languages",
            f"status={admin_langs.status}, body={admin_langs.body}",
        )

        ext = f"x{suffix % 1000000:06d}"[:8]
        new_lang = self._request(
            "POST",
            "/api/admin/languages",
            {
                "name": f"CXX_TMP_{suffix}",
                "extension": ext,
                "compile_cmd": "g++ -O2 -std=c++17 {source} -o {output}",
                "run_cmd": "{binary}",
                "enabled": True,
            },
            headers=admin_headers,
        )
        new_lang_json = self._parse_json(new_lang.body)
        new_lang_id = int(new_lang_json.get("id", 0)) if isinstance(new_lang_json, dict) else 0
        self._check(
            new_lang.status == 201 and new_lang_id > 0,
            "POST /api/admin/languages",
            f"status={new_lang.status}, body={new_lang.body}",
        )

        put_lang = self._request(
            "PUT",
            f"/api/admin/languages/{new_lang_id}",
            {
                "name": f"CXX_TMP_{suffix}_UPD",
                "extension": ext,
                "compile_cmd": "g++ -O2 -std=c++17 {source} -o {output}",
                "run_cmd": "{binary}",
                "enabled": False,
            },
            headers=admin_headers,
        )
        put_lang_json = self._parse_json(put_lang.body)
        self._check(
            put_lang.status == 200 and isinstance(put_lang_json, dict) and put_lang_json.get("ok") is True,
            "PUT /api/admin/languages/{id}",
            f"status={put_lang.status}, body={put_lang.body}",
        )

        ban_user = self._request(
            "PUT",
            f"/api/admin/users/{new_user_id}/status",
            {"status": "banned"},
            headers=admin_headers,
        )
        ban_user_json = self._parse_json(ban_user.body)
        self._check(
            ban_user.status == 200 and isinstance(ban_user_json, dict) and ban_user_json.get("ok") is True,
            "PUT /api/admin/users/{id}/status (banned)",
            f"status={ban_user.status}, body={ban_user.body}",
        )

        unban_user = self._request(
            "PUT",
            f"/api/admin/users/{new_user_id}/status",
            {"status": "active"},
            headers=admin_headers,
        )
        unban_user_json = self._parse_json(unban_user.body)
        self._check(
            unban_user.status == 200 and isinstance(unban_user_json, dict) and unban_user_json.get("ok") is True,
            "PUT /api/admin/users/{id}/status (active)",
            f"status={unban_user.status}, body={unban_user.body}",
        )

        del_problem = self._request("DELETE", f"/api/admin/problems/{admin_problem_id}", headers=admin_headers)
        del_problem_json = self._parse_json(del_problem.body)
        self._check(
            del_problem.status == 200 and isinstance(del_problem_json, dict) and del_problem_json.get("ok") is True,
            "DELETE /api/admin/problems/{id}",
            f"status={del_problem.status}, body={del_problem.body}",
        )

        print("\n=== Summary ===")
        print(f"PASS: {self.pass_count}")
        print(f"FAIL: {self.fail_count}")
        return 0 if self.fail_count == 0 else 1


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="SPEC-driven OJ API smoke test script")
    parser.add_argument("--base-url", default="http://127.0.0.1:8080", help="API base URL, e.g. http://127.0.0.1:8080")
    parser.add_argument("--problem-id", type=int, default=900001, help="Problem ID for problem/submission checks")
    parser.add_argument("--seed-user", default="ui_user_20260526", help="Seeded normal user")
    parser.add_argument("--seed-pass", default="pass1234", help="Seeded normal user password")
    parser.add_argument("--admin-user", default="admin", help="Admin username")
    parser.add_argument("--admin-pass", default="adminpass", help="Admin password")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    tester = ApiSmokeTester(args.base_url)
    return tester.run(
        problem_id=args.problem_id,
        seed_user=args.seed_user,
        seed_pass=args.seed_pass,
        admin_user=args.admin_user,
        admin_pass=args.admin_pass,
    )


if __name__ == "__main__":
    sys.exit(main())
