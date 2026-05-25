#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"

check_contains() {
  local name="$1"
  local body="$2"
  local needle="$3"
  if [[ "$body" == *"$needle"* ]]; then
    printf "[OK] %s\n" "$name"
  else
    printf "[FAIL] %s (missing: %s)\n" "$name" "$needle"
    return 1
  fi
}

printf "Smoke checking frontend-related API against %s\n" "$BASE_URL"

health="$(curl -fsS "$BASE_URL/healthz" || true)"
check_contains "healthz" "$health" "ok"

problems="$(curl -fsS "$BASE_URL/api/problems?limit=12&offset=0&status=published" || true)"
check_contains "problems list has A+B" "$problems" "A + B Problem"
check_contains "problems list has Big Integer" "$problems" "Big Integer Multiply"

problem_detail="$(curl -fsS "$BASE_URL/api/problems/900003" || true)"
check_contains "problem detail has samples" "$problem_detail" "samples"
check_contains "problem detail xss text stored" "$problem_detail" "<script>alert(\"xss\")</script>"

sub_wa="$(curl -fsS "$BASE_URL/api/submissions/920002" || true)"
check_contains "submission WA status" "$sub_wa" "wrong_answer"
check_contains "submission WA expected_output" "$sub_wa" "expected_output"

history="$(curl -fsS "$BASE_URL/api/submissions?user_id=2001&limit=20&offset=0" || true)"
check_contains "history includes compile_error" "$history" "compile_error"
check_contains "history includes accepted" "$history" "accepted"

printf "All smoke checks passed.\n"
