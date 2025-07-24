#pragma once

#include <core/std.h>

struct StringView8
{
	Byte const* ptr;
	PtrSize size_bytes;
};

#define PAW_STR(str)                                                               \
	StringView8                                                                    \
	{                                                                              \
		.ptr = reinterpret_cast<Byte const*>(str), .size_bytes = (sizeof(str) - 1) \
	}

#define PAW_STR_FMT "%.*s"
#define PAW_FMT_STR(str) static_cast<S32>(str.size_bytes), reinterpret_cast<char const*>(str.ptr)