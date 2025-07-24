#pragma once

#include <core/std.h>

struct SrcLocation
{
	char const* file;
	char const* function;
	S32 line;
	S32 column;

	static consteval SrcLocation Get(S32 line = __builtin_LINE(), S32 column = __builtin_COLUMN(), char const* file = __builtin_FILE(), char const* function = __builtin_FUNCTION())
	{
		return {
			.file = file,
			.function = function,
			.line = line,
			.column = column,
		};
	}
};

consteval SrcLocation SrcLoc(SrcLocation src = SrcLocation::Get())
{
	return src;
}