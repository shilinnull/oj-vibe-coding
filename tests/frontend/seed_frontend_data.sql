-- Frontend integration seed data for OJ Vibe Coding
-- Purpose: cover page rendering for problems, detail samples, submission result statuses, and history list.
-- Usage:
--   mysql -u root -p oj < tests/frontend/seed_frontend_data.sql

USE oj;

-- Keep idempotent by cleaning only the reserved test IDs.
DELETE FROM submissions WHERE id BETWEEN 920001 AND 920040;
DELETE FROM test_cases WHERE id BETWEEN 910001 AND 910080;
DELETE FROM problems WHERE id BETWEEN 900001 AND 900010;
DELETE FROM users WHERE id IN (2001, 2002);

INSERT INTO users (id, username, password, email, role, status)
VALUES
  (2001, 'ui_tester', 'PLACEHOLDER_HASH', 'ui_tester@example.com', 'student', 'active'),
  (2002, 'ui_banned', 'PLACEHOLDER_HASH', 'ui_banned@example.com', 'student', 'banned')
ON DUPLICATE KEY UPDATE username = VALUES(username), email = VALUES(email), role = VALUES(role), status = VALUES(status);

INSERT INTO languages (id, name, extension, compile_cmd, run_cmd, enabled)
VALUES
  (1, 'C++17', 'cpp', 'g++ -O2 -std=c++17 {source} -o {output}', '{binary}', 1),
  (2, 'C11', 'c', 'gcc -O2 -std=c11 {source} -o {output}', '{binary}', 1)
ON DUPLICATE KEY UPDATE
  name = VALUES(name),
  extension = VALUES(extension),
  compile_cmd = VALUES(compile_cmd),
  run_cmd = VALUES(run_cmd),
  enabled = VALUES(enabled);

INSERT INTO problems (id, title, description, difficulty, time_limit_ms, memory_limit_kb, status, created_by)
VALUES
  (900001, 'A + B Problem', '给定两个整数，输出它们的和。\n输入: 一行两个整数。\n输出: 一个整数。', 'easy', 1000, 262144, 'published', 2001),
  (900002, 'Array Rotate', '将数组向右旋转 k 位，输出旋转后的数组。\n注意边界条件与大 k。', 'medium', 1500, 262144, 'published', 2001),
  (900003, 'Big Integer Multiply', '实现高精度乘法。\nXSS 检查文本: <script>alert("xss")</script> 应被转义显示。', 'hard', 2500, 524288, 'published', 2001),
  (900004, 'Draft Hidden Problem', '该题用于验证题目列表默认只显示 published。', 'easy', 800, 131072, 'draft', 2001)
ON DUPLICATE KEY UPDATE
  title = VALUES(title),
  description = VALUES(description),
  difficulty = VALUES(difficulty),
  time_limit_ms = VALUES(time_limit_ms),
  memory_limit_kb = VALUES(memory_limit_kb),
  status = VALUES(status),
  created_by = VALUES(created_by);

INSERT INTO test_cases (id, problem_id, is_sample, input, output, sort_order)
VALUES
  (910001, 900001, 1, '1 2\n', '3\n', 1),
  (910002, 900001, 1, '-5 10\n', '5\n', 2),
  (910003, 900001, 0, '123 456\n', '579\n', 3),

  (910011, 900002, 1, '5 2\n1 2 3 4 5\n', '4 5 1 2 3\n', 1),
  (910012, 900002, 1, '4 6\n9 8 7 6\n', '7 6 9 8\n', 2),
  (910013, 900002, 0, '3 1\n4 5 6\n', '6 4 5\n', 3),

  (910021, 900003, 1, '123456789\n987654321\n', '121932631112635269\n', 1),
  (910022, 900003, 0, '999999999999\n999999999999\n', '999999999998000000000001\n', 2)
ON DUPLICATE KEY UPDATE
  problem_id = VALUES(problem_id),
  is_sample = VALUES(is_sample),
  input = VALUES(input),
  output = VALUES(output),
  sort_order = VALUES(sort_order);

-- Submission statuses coverage for result page and history page.
INSERT INTO submissions (id, user_id, problem_id, language_id, source_code, mode, status, result_json, time_ms, memory_kb, created_at)
VALUES
  (
    920001,
    2001,
    900001,
    1,
    '#include <bits/stdc++.h>\nint main(){long long a,b;std::cin>>a>>b;std::cout<<(a+b)<<"\\n";}',
    'submit',
    'accepted',
    '{"status":"ACCEPTED","compile_error":null,"results":[{"test_case_id":910001,"status":"ACCEPTED","time_ms":1,"memory_kb":1024,"actual_output":"3\\n","expected_output":"3\\n"},{"test_case_id":910002,"status":"ACCEPTED","time_ms":1,"memory_kb":1024,"actual_output":"5\\n","expected_output":"5\\n"}],"summary":{"total":2,"passed":2,"total_time_ms":2,"peak_memory_kb":1024}}',
    2,
    1024,
    NOW() - INTERVAL 55 MINUTE
  ),
  (
    920002,
    2001,
    900001,
    1,
    'int main(){return 0;}',
    'submit',
    'wrong_answer',
    '{"status":"WRONG_ANSWER","compile_error":null,"results":[{"test_case_id":910001,"status":"WRONG_ANSWER","time_ms":1,"memory_kb":1024,"actual_output":"4\\n","expected_output":"3\\n"}],"summary":{"total":1,"passed":0,"total_time_ms":1,"peak_memory_kb":1024}}',
    1,
    1024,
    NOW() - INTERVAL 50 MINUTE
  ),
  (
    920003,
    2001,
    900002,
    1,
    'int main(){while(1){}}',
    'submit',
    'time_limit_exceeded',
    '{"status":"TIME_LIMIT_EXCEEDED","compile_error":null,"results":[{"test_case_id":910011,"status":"TIME_LIMIT_EXCEEDED","time_ms":1501,"memory_kb":1200,"actual_output":"","expected_output":"4 5 1 2 3\\n"}],"summary":{"total":1,"passed":0,"total_time_ms":1501,"peak_memory_kb":1200}}',
    1501,
    1200,
    NOW() - INTERVAL 45 MINUTE
  ),
  (
    920004,
    2001,
    900002,
    1,
    'int main(){std::vector<int> v; while(true) v.push_back(1);}',
    'submit',
    'memory_limit_exceeded',
    '{"status":"MEMORY_LIMIT_EXCEEDED","compile_error":null,"results":[{"test_case_id":910011,"status":"MEMORY_LIMIT_EXCEEDED","time_ms":33,"memory_kb":300000,"actual_output":"","expected_output":"4 5 1 2 3\\n"}],"summary":{"total":1,"passed":0,"total_time_ms":33,"peak_memory_kb":300000}}',
    33,
    300000,
    NOW() - INTERVAL 40 MINUTE
  ),
  (
    920005,
    2001,
    900002,
    1,
    'int main(){int a=1/0;}',
    'submit',
    'runtime_error',
    '{"status":"RUNTIME_ERROR","compile_error":null,"results":[{"test_case_id":910011,"status":"RUNTIME_ERROR","time_ms":2,"memory_kb":1100,"actual_output":"","expected_output":"4 5 1 2 3\\n"}],"summary":{"total":1,"passed":0,"total_time_ms":2,"peak_memory_kb":1100}}',
    2,
    1100,
    NOW() - INTERVAL 35 MINUTE
  ),
  (
    920006,
    2001,
    900003,
    1,
    'int main(){syntax error}',
    'submit',
    'compile_error',
    '{"status":"COMPILE_ERROR","compile_error":"main.cpp: In function ''int main()'':\\nmain.cpp:1:12: error: expected '';'' before ''error'' token","results":[],"summary":{"total":0,"passed":0,"total_time_ms":0,"peak_memory_kb":0}}',
    0,
    0,
    NOW() - INTERVAL 30 MINUTE
  ),
  (
    920007,
    2001,
    900003,
    1,
    'int main(){return 0;}',
    'submit',
    'system_error',
    '{"status":"SYSTEM_ERROR","compile_error":null,"results":[],"summary":{"total":0,"passed":0,"total_time_ms":0,"peak_memory_kb":0}}',
    0,
    0,
    NOW() - INTERVAL 25 MINUTE
  ),
  (
    920008,
    2001,
    900001,
    1,
    'int main(){return 0;}',
    'run',
    'running',
    '{"status":"RUNNING","mode":"run","judge_scope":"sample","sample_case_count":2}',
    0,
    0,
    NOW() - INTERVAL 20 MINUTE
  ),
  (
    920009,
    2001,
    900001,
    1,
    'int main(){return 0;}',
    'submit',
    'pending',
    '{"status":"PENDING","mode":"submit","judge_scope":"all","sample_case_count":2}',
    0,
    0,
    NOW() - INTERVAL 15 MINUTE
  ),
  (
    920010,
    2001,
    900002,
    2,
    '#include <stdio.h>\nint main(){int x; if(scanf("%d", &x)!=1) return 0; printf("%d\\n", x); return 0;}',
    'run',
    'accepted',
    '{"status":"ACCEPTED","compile_error":null,"results":[{"test_case_id":910011,"status":"ACCEPTED","time_ms":1,"memory_kb":900,"actual_output":"4 5 1 2 3\\n","expected_output":"4 5 1 2 3\\n"}],"summary":{"total":1,"passed":1,"total_time_ms":1,"peak_memory_kb":900}}',
    1,
    900,
    NOW() - INTERVAL 10 MINUTE
  )
ON DUPLICATE KEY UPDATE
  user_id = VALUES(user_id),
  problem_id = VALUES(problem_id),
  language_id = VALUES(language_id),
  source_code = VALUES(source_code),
  mode = VALUES(mode),
  status = VALUES(status),
  result_json = VALUES(result_json),
  time_ms = VALUES(time_ms),
  memory_kb = VALUES(memory_kb),
  created_at = VALUES(created_at);
