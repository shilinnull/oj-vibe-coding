#include <gtest/gtest.h>

#include <string>

#include <nlohmann/json.hpp>

#include "utils/json_helper.h"

TEST(JsonHelperTest, ParsesValidJson) {
	std::string error;
	auto parsed = oj::TryParseJson(R"({"name":"oj","count":3})", &error);

	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ((*parsed)["name"], "oj");
	EXPECT_EQ((*parsed)["count"], 3);
	EXPECT_TRUE(error.empty());
}

TEST(JsonHelperTest, ReportsInvalidJsonError) {
	std::string error;
	auto parsed = oj::TryParseJson(R"({"name":)", &error);

	EXPECT_FALSE(parsed.has_value());
	EXPECT_FALSE(error.empty());
}

TEST(JsonHelperTest, DumpsPrettyJson) {
	nlohmann::json value = {{"name", "oj"}, {"count", 3}};

	const std::string dumped = oj::DumpJson(value, 2);
	EXPECT_NE(dumped.find("\n"), std::string::npos);
	EXPECT_NE(dumped.find("\"name\""), std::string::npos);
}