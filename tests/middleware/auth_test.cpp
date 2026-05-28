#include <gtest/gtest.h>

#include <string>

#include "middleware/auth.h"

TEST(AuthTest, JwtRoundTripAndExpiry) {
	const std::string secret = "test-secret";
	const std::string token = oj::GenerateJwt(secret, 42, "alice", "student", 3600);

	auto info = oj::VerifyJwt(secret, token);
	ASSERT_TRUE(info.has_value());
	EXPECT_EQ(info->user_id, 42);
	EXPECT_EQ(info->username, "alice");
	EXPECT_EQ(info->role, "student");
}

TEST(AuthTest, JwtRejectsTampering) {
	const std::string secret = "test-secret";
	std::string token = oj::GenerateJwt(secret, 42, "alice", "student", 3600);
	token.back() = token.back() == 'a' ? 'b' : 'a';

	EXPECT_FALSE(oj::VerifyJwt(secret, token).has_value());
}

TEST(AuthTest, JwtRejectsExpiredToken) {
	const std::string secret = "test-secret";
	const std::string token = oj::GenerateJwt(secret, 42, "alice", "student", -1);

	EXPECT_FALSE(oj::VerifyJwt(secret, token).has_value());
}

TEST(AuthTest, AuthenticateRequestReadsBearerToken) {
	const std::string secret = "test-secret";
	const std::string token = oj::GenerateJwt(secret, 7, "bob", "admin", 3600);

	http::Request req;
	req._headers.emplace("Authorization", "Bearer " + token);

	auto info = oj::AuthenticateRequest(secret, req);
	ASSERT_TRUE(info.has_value());
	EXPECT_EQ(info->user_id, 7);
	EXPECT_EQ(info->username, "bob");
	EXPECT_EQ(info->role, "admin");
}
