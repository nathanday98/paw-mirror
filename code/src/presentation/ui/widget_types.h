#pragma once

#include <core/std.h>
#include <core/math_types.h>

struct ParentConstraints
{
	Float2 min_size;
	Float2 max_size;
};

struct Size
{
	enum class Type
	{
		Pixel,
		Fractional,
		ShrinkWrap,
	};

	Type type;
	F32 value;

	static Size pixel(F32 value);
	static Size fractional(F32 value);
	static Size shrink_wrap();

	// Alias for fractional(1.0f);
	static Size fill();
};

struct Offset
{
	enum class Type
	{
		Pixel,
		ParentSizeRelative,
		ChildSizeRelative,
	};

	Type type;
	F32 value;

	static Offset pixel(F32 value);
	static Offset parent_size_relative(F32 value);
	static Offset child_size_relative(F32 value);
};

struct Widget;