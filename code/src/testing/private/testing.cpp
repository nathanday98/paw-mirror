#include <testing/testing.h>

#include <core/assert.h>
#include <core/memory.h>
#include <core/slice.inl>

#include <cstdio>
#include <stdint.h>
#include <string.h>

#include <Windows.h>

#include "testing_platform.h"

#define COLOR_RED "\033[1;31m"
#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[0;32m"

static TestCase const* g_context = nullptr;
static bool test_passed = true;

void fail_test_case_equal(int line)
{
	std::fprintf(stderr, COLOR_RED "Failed %s:%s:%s" COLOR_RESET "\nFile: %s\nLine: %d\n", g_context->project, g_context->module, g_context->name, g_context->file, line);
	test_passed = false;
	if (platform_is_debugger_present())
	{
		__debugbreak();
	}
}

void AssertFunc(char const* file, U32 line, char const* expression, char const* /*message*/)
{
	std::fprintf(stderr, COLOR_RED "Failed %s:%s:%s" COLOR_RESET "\nDue to an assert in File: %s\nLine: %d\nExpression: %s\n", g_context->project, g_context->module, g_context->name, file, line, expression);
	test_passed = false;
}

CoreAssertFunc* g_core_assert_func = &AssertFunc;

class TestLocator : NonCopyable
{
public:
	static TestLocator& get_instance()
	{
		static TestLocator instance{};
		return instance;
	}

	void register_test(TestCase* test)
	{
		if (first_test == nullptr)
		{
			first_test = test;
			current_test = test;
		}
		else
		{
			current_test->next_test = test;
			current_test = test;
		}
	}

	TestCase const* get_first_test() const
	{
		return first_test;
	}

private:
	TestCase* first_test = nullptr;
	TestCase* current_test = nullptr;
};

int test_main(int arg_count, char* args[])
{
	platform_setup_console();

	MemoryInit();

	if (arg_count > 1 && strcmp("-list", args[1]) == 0)
	{
		for (TestCase const* test = TestLocator::get_instance().get_first_test(); test != nullptr; test = test->next_test)
		{
			fprintf(stdout, "%s::%s::%s - %s::%d\n", test->project, test->module, test->name, test->file, test->line);
		}
	}
	else
	{
		int failed_count = 0;
		int total_count = 0;
		for (TestCase const* test = TestLocator::get_instance().get_first_test(); test != nullptr; test = test->next_test)
		{
			test_passed = true;
			g_context = test;
			test->function();
			g_context = nullptr;
			failed_count += !test_passed;
			total_count++;
		}

		fprintf(stdout, "Completed %d/%d tests\n", total_count - failed_count, total_count);

		if (failed_count > 0)
		{
			fprintf(stderr, COLOR_RED "Failed %d/%d tests\n" COLOR_RESET, failed_count, total_count);
			return -1;
		}
		else
		{
			fprintf(stdout, COLOR_GREEN "All Tests Succeeded!\n" COLOR_RESET);
		}
	}

	MemoryDeinit();

	return 0;
}

TestCase::TestCase(char const* project, char const* file, char const* module, char const* name, TestCastFunc* function, int line)
	: project(project)
	, file(file)
	, module(module)
	, name(name)
	, function(function)
	, line(line)
	, next_test(nullptr)
{
	TestLocator::get_instance().register_test(this);
}
