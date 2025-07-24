#pragma once

#include <core/std.h>
#include <core/assert.h>

#include <initializer_list>

template <typename T>
struct Slice
{
	T* items = nullptr;
	S32 count = 0;

	Slice() = default;

	Slice(T* items, S32 count)
		: items(items)
		, count(count)
	{
	}

	Slice(std::initializer_list<T> init_list)
		: items(init_list.begin())
		, count(static_cast<S32>(init_list.size()))
	{
	}

	T& operator[](S32 index)
	{
		PAW_ASSERT(index >= 0 && index < count, "index is not in range");
		return items[index];
	}

	T& operator[](S32 index) const
	{
		PAW_ASSERT(index >= 0 && index < count, "index is not in range");
		return items[index];
	}
};

template <typename T>
struct Slice2D
{
	Slice2D() = default;
	Slice2D(T* items, S32 row_count, S32 column_count)
		: items(items)
		, row_count(row_count)
		, column_count(column_count)
	{
	}
	// Should only be used in when const ptr. I tried using enable_if, but it gave a less clear error

	// #TODO: Maybe in non-debug builds this should return a pointer
	T* operator[](S32 row_index)
	{
		PAW_ASSERT(row_index < row_count, "Row Index is not in range");
		return items + (row_index * column_count);
	}

	T* const operator[](S32 row_index) const
	{
		PAW_ASSERT(row_index < row_count, "Row Index is not in range");
		return items + (row_index * column_count);
	}

	Slice<T> row(S32 row_index)
	{
		PAW_ASSERT(row_index < row_count, "Row Index is not in range");
		return Slice<T>{items + (row_index * column_count), column_count};
	}

	Slice<T const> row(S32 row_index) const
	{
		PAW_ASSERT(row_index < row_count, "Row Index is not in range");
		return Slice<T const>{items + (row_index * column_count), column_count};
	}

	T* items = nullptr;
	S32 row_count = 0;
	S32 column_count = 0;
};
