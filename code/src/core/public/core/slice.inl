#pragma once

#include <core/slice_types.h>
#include <core/assert.h>
// #include <core/initializer_list.h>

#include <initializer_list>

template <typename T>
inline Slice<T> SliceFromStartEnd(T* start, T* end)
{
	PtrSize const size_Bytes = reinterpret_cast<PtrSize>(end) - reinterpret_cast<PtrSize>(start);
	PtrSize const size_items = size_Bytes / sizeof(T);

	return Slice<T>{start, static_cast<S32>(size_items)};
}

template <typename T>
inline Slice<T const> ConstSliceFromStartEnd(T* start, T* end)
{
	PtrSize const size_Bytes = reinterpret_cast<PtrSize>(end) - reinterpret_cast<PtrSize>(start);
	PtrSize const size_items = size_Bytes / sizeof(T);

	return Slice<T const>{start, static_cast<S32>(size_items)};
}

template <typename T>
inline PtrSize CalcTotalSizeBytes(Slice<T> slice)
{
	PAW_ASSERT(slice.count >= 0, "zero elements in slice");
	return static_cast<PtrSize>(slice.count * sizeof(T));
}

template <typename T>
inline Slice<T const> ToConstSlice(Slice<T> slice)
{
	return Slice<T const>{slice.items, slice.count};
}

template <typename T>
inline Slice<T> SubSlice(Slice<T> slice, S32 sub_start, S32 sub_count)
{
	PAW_ASSERT(sub_start >= 0 && sub_count >= 0 && sub_start + sub_count <= slice.count, "sub slice is not a valid range");
	return {slice.items + sub_start, sub_count};
}

template <typename T>
inline Slice<T const> ConstSubSlice(Slice<T> slice, S32 sub_start, S32 sub_count)
{
	PAW_ASSERT(sub_start >= 0 && sub_count >= 0 && sub_start + sub_count <= slice.count, "sub slice is not a valid range");
	return {slice.items + sub_start, sub_count};
}

template <typename T>
inline PtrSize CalcTotalSizeBytes(Slice2D<T> slice)
{
	S32 const count = slice.row_count * slice.column_count;
	PAW_ASSERT(count >= 0, "zero elements in slice");
	return static_cast<PtrSize>(count * sizeof(T));
}

template <typename T>
inline T* begin(Slice<T> const& slice)
{
	return slice.items;
}

template <typename T>
inline T* end(Slice<T> const& slice)
{
	return slice.items + slice.count;
}

template <typename T>
inline T const* begin(Slice<T const> const& slice)
{
	return slice.items;
}

template <typename T>
inline T const* end(Slice<T const> const& slice)
{
	return slice.items + slice.count;
}

// When using this, use --it instead of ++it!
template <typename T>
inline T* rbegin(Slice<T>& slice)
{
	return slice.items + (slice.count - 1);
}

template <typename T>
inline T* rend(Slice<T>& slice)
{
	return slice.items - 1;
}

template <typename T>
inline T const* rbegin(Slice<T> const& slice)
{
	return slice.items + (slice.count - 1);
}

template <typename T>
inline T const* rend(Slice<T> const& slice)
{
	return slice.items - 1;
}

template <typename T>
inline T& First(Slice<T>& slice)
{
	return slice.items[0];
}

template <typename T>
inline T& Last(Slice<T>& slice)
{
	return slice.items[slice.count - 1];
}

template <typename T>
inline T& First(Slice<T> const& slice)
{
	return slice.items[0];
}

template <typename T>
inline T& Last(Slice<T> const& slice)
{
	return slice.items[slice.count - 1];
}

template <typename T>
struct ReverseIteratorPointer_t
{
	T* ptr;

	constexpr ReverseIteratorPointer_t& operator++()
	{
		--ptr;
		return *this;
	}

	constexpr auto& operator*() const
	{
		return *ptr;
	}

	constexpr bool operator==(ReverseIteratorPointer_t<T> const& rhs) const = default;
};

template <typename T>
struct ReverseIterator_t
{
	Slice<T>& slice;

	constexpr ReverseIteratorPointer_t<T> begin()
	{
		return {&slice.items[slice.count - 1]};
	}

	constexpr ReverseIteratorPointer_t<T> end()
	{
		return {&slice.items[-1]};
	}
};

template <typename T>
constexpr ReverseIterator_t<T> ReverseIterator(Slice<T>& slice)
{
	return ReverseIterator_t{slice};
}