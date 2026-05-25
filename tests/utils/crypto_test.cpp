#include <gtest/gtest.h>

#include <string>

#include "utils/crypto.h"

TEST(CryptoTest, PasswordHashRoundTrip) {
	const std::string password = "S3cure-P@ssw0rd!";
	const std::string hash = oj::GeneratePasswordHash(password);

	EXPECT_FALSE(hash.empty());
	EXPECT_TRUE(oj::VerifyPassword(password, hash));
	EXPECT_FALSE(oj::VerifyPassword("wrong-password", hash));
}

TEST(CryptoTest, Base64UrlEncodeDecodeRoundTrip) {
	const std::string original = R"({"sub":123,"username":"alice"})";
	const std::string encoded = oj::Base64UrlEncode(original);
	auto decoded = oj::Base64UrlDecode(encoded);

	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(*decoded, original);
}

TEST(CryptoTest, Base64UrlDecodeRejectsInvalidData) {
	auto decoded = oj::Base64UrlDecode("***not-valid***");
	EXPECT_FALSE(decoded.has_value());
}
