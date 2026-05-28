#!/usr/bin/env python3
"""Repeatedly submit the same source code to the OJ server.

Example:
  python3 scripts/repeat_submit.py \
    --base-url http://127.0.0.1:8080 \
    --problem-id 1 \
    --user-id 1 \
    --language-id 1 \
    --count 10 \
    --source-file ./sample.cpp
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any, Optional


class ApiClient:
	def __init__(self, base_url: str, timeout: float = 8.0) -> None:
		self.base_url = base_url.rstrip("/")
		self.timeout = timeout

	def request(
		self,
		method: str,
		path: str,
		payload: Optional[dict[str, Any]] = None,
	) -> tuple[int, str]:
		url = f"{self.base_url}{path}"
		headers: dict[str, str] = {}
		data: Optional[bytes] = None
		if payload is not None:
			data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
			headers["Content-Type"] = "application/json"
		req = urllib.request.Request(url=url, data=data, headers=headers, method=method)
		try:
			with urllib.request.urlopen(req, timeout=self.timeout) as resp:
				return resp.getcode(), resp.read().decode("utf-8", errors="replace")
		except urllib.error.HTTPError as exc:
			return exc.code, exc.read().decode("utf-8", errors="replace")

	def submit(self, payload: dict[str, Any]) -> dict[str, Any]:
		status, body = self.request("POST", "/api/submissions", payload)
		if status not in (200, 201):
			raise RuntimeError(f"submit failed: status={status}, body={body}")
		return json.loads(body)

	def get_submission(self, submission_id: int) -> dict[str, Any]:
		status, body = self.request("GET", f"/api/submissions/{submission_id}")
		if status != 200:
			raise RuntimeError(f"fetch submission failed: status={status}, body={body}")
		return json.loads(body)


def load_source(args: argparse.Namespace) -> str:
	if args.source_file:
		return Path(args.source_file).read_text(encoding="utf-8")
	if args.source:
		return args.source
	return "#include <iostream>\nint main(){std::cout << 3 << '\\n'; return 0;}\n"


def is_finished(status: str) -> bool:
	state = status.lower()
	return not ("pending" in state or "running" in state)


def main() -> int:
	parser = argparse.ArgumentParser(description="Repeatedly submit the same code for judging")
	parser.add_argument("--base-url", default="http://127.0.0.1:8080", help="OJ server base URL")
	parser.add_argument("--problem-id", type=int, required=True, help="Problem id")
	parser.add_argument("--user-id", type=int, required=True, help="Submitting user id")
	parser.add_argument("--language-id", type=int, required=True, help="Language id")
	parser.add_argument("--count", type=int, default=10, help="Number of submissions")
	parser.add_argument("--interval", type=float, default=0.0, help="Seconds between submissions")
	parser.add_argument("--poll-interval", type=float, default=0.0, help="Seconds between polling requests")
	parser.add_argument("--timeout", type=float, default=120.0, help="Max seconds to wait for each submission")
	parser.add_argument("--mode", default="run", choices=["run", "submit"], help="Submission mode")
	parser.add_argument("--source-file", help="Path to source code file")
	parser.add_argument("--source", help="Inline source code")
	args = parser.parse_args()

	source_code = load_source(args)
	client = ApiClient(args.base_url)
	results: list[dict[str, Any]] = []

	for index in range(1, args.count + 1):
		payload = {
			"user_id": args.user_id,
			"problem_id": args.problem_id,
			"language_id": args.language_id,
			"source_code": source_code,
			"mode": args.mode,
		}
		create = client.submit(payload)
		submission_id = int(create.get("id", 0))
		if submission_id <= 0:
			raise RuntimeError(f"submission id missing in response: {create}")

		print(f"[{index}/{args.count}] submitted id={submission_id}")

		deadline = time.monotonic() + args.timeout
		final_data: dict[str, Any] = {}
		while True:
			current = client.get_submission(submission_id)
			status = str(current.get("status", ""))
			result_status = str((current.get("result") or {}).get("status", status))
			print(f"  status={status}, result={result_status}")
			final_data = current
			if is_finished(status) and is_finished(result_status):
				break
			if time.monotonic() >= deadline:
				raise TimeoutError(f"submission {submission_id} still not finished after {args.timeout}s")
			time.sleep(args.poll_interval)

		results.append(final_data)
		final_status = final_data.get("status", "")
		result_status = (final_data.get("result") or {}).get("status", "")
		print(f"  final: id={submission_id}, status={final_status}, result={result_status}")

		if index < args.count and args.interval > 0:
			time.sleep(args.interval)

	print("\nSummary:")
	for item in results:
		item_id = item.get("id", "-")
		status = item.get("status", "-")
		result = (item.get("result") or {}).get("status", "-")
		print(f"  id={item_id} status={status} result={result}")

	return 0


if __name__ == "__main__":
	raise SystemExit(main())