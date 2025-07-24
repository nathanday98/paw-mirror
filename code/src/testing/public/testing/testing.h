#pragma once

#include <core/std.h>

#include <testing/testing_types.h>

typedef void TestCastFunc();

class TestCase : NonCopyable
{
public:
	TestCase(char const* project, char const* file, char const* module, char const* name, TestCastFunc* function, int line);

	// private:
	char const* const project;
	char const* const file;
	char const* const module;
	char const* const name;
	TestCastFunc* const function;
	int const line;
	TestCase* next_test;
};

#define PAW_TEST_CONCAT_EX(x, y) x##y
#define PAW_TEST_CONCAT(x, y) PAW_TEST_CONCAT_EX(x, y)

#define PAW_TEST_FUNC_NAME(name) PAW_TEST_CONCAT(PAW_TEST_CONCAT(name, _test_func), __LINE__)

#define PAW_TEST_STRINGIFY_EX(x) #x
#define PAW_TEST_STRINGIFY(x) PAW_TEST_STRINGIFY_EX(x)

#define PAW_TEST_VAR_NAME(name) PAW_TEST_CONCAT(g_, PAW_TEST_CONCAT(PAW_TEST_CONCAT(PAW_TEST_MODULE_NAME, name), __LINE__))

#define PAW_TEST(name)                             \
	static void PAW_TEST_FUNC_NAME(name)();        \
	static TestCase PAW_TEST_VAR_NAME(name){       \
		PAW_TEST_STRINGIFY(PAW_TEST_PROJECT_NAME), \
		__FILE__,                                  \
		PAW_TEST_STRINGIFY(PAW_TEST_MODULE_NAME),  \
		#name,                                     \
		&PAW_TEST_FUNC_NAME(name),                 \
		__LINE__,                                  \
	};                                             \
	static void PAW_TEST_FUNC_NAME(name)()

void fail_test_case_equal(int line);

template <typename T>
void test_expect_equal(T const& value, T const& expected, int line)
{
	if (value != expected)
	{
		fail_test_case_equal(line);
	}
}

#define PAW_TEST_EXPECT_EQUAL(value, expected) test_expect_equal(value, expected, __LINE__)

#define PAW_TEST_EXPECT(bool_expression) test_expect_equal<bool>(bool_expression, true, __LINE__)
#define PAW_TEST_EXPECT_NOT(bool_expression) test_expect_equal<bool>(!bool_expression, true, __LINE__)

int test_main(int arg_count, char* args[]);