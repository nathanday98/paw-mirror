#pragma once

#include <core/std.h>

typedef void CoreAssertFunc(char const* file, U32 line, char const* expression, char const* message);
extern CoreAssertFunc* g_core_assert_func;

#ifdef PAW_DEBUG
#define PAW_ASSERT(expression, message)                                   \
	if (!(expression))                                                    \
	{                                                                     \
		if (true)                                                         \
		{                                                                 \
			g_core_assert_func(__FILE__, __LINE__, #expression, message); \
			__builtin_debugtrap();                                        \
		}                                                                 \
		else                                                              \
		{                                                                 \
		}                                                                 \
	}                                                                     \
	else                                                                  \
	{                                                                     \
	}

#define PAW_UNREACHABLE \
	do                  \
	{                   \
		__debugbreak(); \
	} while (false);
#else
#define PAW_ASSERT(expression)      \
	do                              \
	{                               \
		(void)sizeof((expression)); \
	} while (false)
#define PAW_UNREACHABLE
#endif