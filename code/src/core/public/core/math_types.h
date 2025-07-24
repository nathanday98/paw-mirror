#pragma once

#include <core/std.h>

struct Float2
{
	F32 x = 0.0f;
	F32 y = 0.0f;

	F32 operator[](S32 index) const;
	F32& operator[](S32 index);
};

struct Float3
{
	F32 x = 0.0f;
	F32 y = 0.0f;
	F32 z = 0.0f;

	F32 operator[](S32 index) const;
	F32& operator[](S32 index);
};

struct Float4
{
	F32 x = 0.0f;
	F32 y = 0.0f;
	F32 z = 0.0f;
	F32 w = 0.0f;

	F32 operator[](S32 index) const;
	F32& operator[](S32 index);
};

struct Int2
{
	S32 x = 0;
	S32 y = 0;
};