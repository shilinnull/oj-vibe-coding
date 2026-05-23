-- OJ Vibe Coding - init schema
-- Usage example:
--   mysql -u shilin -p123456 < scripts/init_db.sql

CREATE DATABASE IF NOT EXISTS oj DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE oj;

-- Users
CREATE TABLE IF NOT EXISTS users (
	id          BIGINT AUTO_INCREMENT PRIMARY KEY,
	username    VARCHAR(64)  NOT NULL UNIQUE,
	password    VARCHAR(256) NOT NULL,
	email       VARCHAR(128) NOT NULL DEFAULT '',
	role        ENUM('student', 'admin') DEFAULT 'student',
	status      ENUM('active', 'banned') DEFAULT 'active',
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
	updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Languages
CREATE TABLE IF NOT EXISTS languages (
	id          INT AUTO_INCREMENT PRIMARY KEY,
	name        VARCHAR(32)  NOT NULL,
	extension   VARCHAR(8)   NOT NULL,
	compile_cmd VARCHAR(512) NOT NULL,
	run_cmd     VARCHAR(512) NOT NULL,
	enabled     TINYINT(1) DEFAULT 1,
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- Problems
CREATE TABLE IF NOT EXISTS problems (
	id              BIGINT AUTO_INCREMENT PRIMARY KEY,
	title           VARCHAR(256) NOT NULL,
	description     TEXT         NOT NULL,
	difficulty      ENUM('easy', 'medium', 'hard') DEFAULT 'medium',
	time_limit_ms   INT          NOT NULL DEFAULT 1000,
	memory_limit_kb INT          NOT NULL DEFAULT 262144,
	status          ENUM('draft', 'published', 'archived') DEFAULT 'draft',
	created_by      BIGINT,
	created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
	updated_at      DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
	FOREIGN KEY (created_by) REFERENCES users(id)
) ENGINE=InnoDB;

-- Test cases
CREATE TABLE IF NOT EXISTS test_cases (
	id          BIGINT AUTO_INCREMENT PRIMARY KEY,
	problem_id  BIGINT NOT NULL,
	is_sample   TINYINT(1) DEFAULT 0,
	input       TEXT   NOT NULL,
	output      TEXT   NOT NULL,
	sort_order  INT DEFAULT 0,
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
	FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- Submissions
CREATE TABLE IF NOT EXISTS submissions (
	id          BIGINT AUTO_INCREMENT PRIMARY KEY,
	user_id     BIGINT NOT NULL,
	problem_id  BIGINT NOT NULL,
	language_id INT    NOT NULL,
	source_code TEXT   NOT NULL,
	mode        ENUM('run', 'submit') DEFAULT 'submit',
	status      ENUM('pending', 'running', 'accepted', 'wrong_answer',
					 'time_limit_exceeded', 'memory_limit_exceeded',
					 'runtime_error', 'compile_error', 'system_error')
					 DEFAULT 'pending',
	result_json JSON,
	time_ms     INT DEFAULT 0,
	memory_kb   INT DEFAULT 0,
	created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
	FOREIGN KEY (user_id)     REFERENCES users(id),
	FOREIGN KEY (problem_id)  REFERENCES problems(id),
	FOREIGN KEY (language_id) REFERENCES languages(id)
) ENGINE=InnoDB;

-- Indexes
CREATE INDEX idx_submissions_user_id ON submissions(user_id);
CREATE INDEX idx_submissions_problem_id ON submissions(problem_id);
CREATE INDEX idx_test_cases_problem_id ON test_cases(problem_id);
CREATE INDEX idx_problems_status ON problems(status);

-- Seed data
-- NOTE: 密码应存储为 bcrypt/argon2 hash。P0 阶段先放占位值，后续在 P2 crypto 完成后再替换。
INSERT IGNORE INTO users(username, password, email, role, status)
VALUES ('admin', 'CHANGE_ME_HASH', '', 'admin', 'active');

INSERT IGNORE INTO languages(id, name, extension, compile_cmd, run_cmd, enabled)
VALUES
  (1, 'C++17', 'cpp', 'g++ -O2 -std=c++17 {source} -o {output}', '{binary}', 1),
  (2, 'C11', 'c', 'gcc -O2 -std=c11 {source} -o {output}', '{binary}', 1);
