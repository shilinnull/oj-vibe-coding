cmake_minimum_required(VERSION 3.20)

project(oj_vibe_coding LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_ROOT ${CMAKE_BINARY_DIR})

option(OJ_BUILD_TESTS "Build unit tests" ON)

find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# MySQL C client library
find_package(PkgConfig REQUIRED)
pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient)

add_library(oj_core STATIC)
target_sources(oj_core
	PRIVATE
		src/utils/config.cpp
		src/utils/logger.cpp
		src/utils/crypto.cpp
		src/utils/json_helper.cpp
		src/db/mysql_pool.cpp
		src/db/dao/user_dao.cpp
		src/db/dao/problem_dao.cpp
		src/db/dao/submission_dao.cpp
		src/db/dao/test_case_dao.cpp
		src/db/dao/language_dao.cpp
		src/model/user.cpp
		src/model/problem.cpp
		src/model/submission.cpp
		src/model/test_case.cpp
		src/model/language.cpp
		src/server/server.cpp
		src/net/router.cpp
		src/middleware/auth.cpp
		src/handler/auth_handler.cpp
		src/handler/problem_handler.cpp
		src/handler/submission_handler.cpp
		src/handler/admin_handler.cpp
		src/judge/judge_manager.cpp
)

target_include_directories(oj_core
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/src
		${MYSQLCLIENT_INCLUDE_DIRS}
)

target_link_libraries(oj_core
	PUBLIC
		yaml-cpp::yaml-cpp
		nlohmann_json::nlohmann_json
		${MYSQLCLIENT_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_compile_options(oj_core PRIVATE -Wall -Wextra -Wpedantic)

# Server binary in build/
add_executable(oj_server src/main.cpp)
target_link_libraries(oj_server PRIVATE oj_core)
set_target_properties(oj_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT})

# judger_cli placed in source run/ so tests and scripts find it at ./run/judger_cli
add_executable(judger_cli src/judge/judger_cli.cpp)
target_link_libraries(judger_cli PRIVATE oj_core nlohmann_json::nlohmann_json)
set_target_properties(judger_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/run)

# judge worker binary in build/
add_executable(judge_worker src/judge/judge_worker_server.cpp)
target_link_libraries(judge_worker PRIVATE oj_core nlohmann_json::nlohmann_json)
set_target_properties(judge_worker PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT})

# sandbox preload library in build/
add_library(judge_sandbox_preload SHARED src/judge/sandbox_preload.cpp)
target_link_libraries(judge_sandbox_preload PRIVATE dl)
set_target_properties(judge_sandbox_preload PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/run)

# tools -> build/ (no source tree copies)
add_executable(register_admin tools/register_admin.cpp)
target_link_libraries(register_admin PRIVATE oj_core)
set_target_properties(register_admin PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT})

add_executable(reset_web_data tools/reset_web_data.cpp)
target_link_libraries(reset_web_data PRIVATE oj_core)
set_target_properties(reset_web_data PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT})

if(OJ_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    # Keep unit tests under tests/ and auto-pick *_test.cpp recursively.
    file(GLOB_RECURSE OJ_TEST_SOURCES CONFIGURE_DEPENDS
        tests/*_test.cpp
    )

    add_executable(oj_tests
        tests/main.cpp
        ${OJ_TEST_SOURCES}
    )

    target_link_libraries(oj_tests PRIVATE oj_core GTest::gtest GTest::gtest_main)

    add_test(NAME oj_tests COMMAND oj_tests)
endif()
