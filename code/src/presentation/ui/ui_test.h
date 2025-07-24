#pragma once

#include <core/memory_types.h>
#include <core/math_types.h>
#include <core/slice_types.h>

struct RenderClipRect
{
	Float2 position{};
	Float2 size{};
};

struct RenderBox
{
	Float2 position{};
	Float2 size{};
	Float4 color{};
};

struct RenderLine
{
	Float2 start{};
	Float2 end{};
	F32 thickness = 1.0f;
	Float4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct RenderItem
{
	enum class Type
	{
		Box,
		Line,
	};

	Type type = Type::Box;
	RenderClipRect clip_rect;
	union
	{
		RenderBox box{};
		RenderLine line;
	};
};

void UITest(IAllocator* allocator);
Slice<RenderItem const> UIUpdate(Float2 ui_size);