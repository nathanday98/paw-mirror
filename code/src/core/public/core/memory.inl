#pragma once

#include <core/memory.h>
#include <core/slice.inl>

#define PAW_NEW(type) ::new (alignof(type), nullptr, SrcLoc()) type
#define PAW_NEW_IN(allocator, type) ::new (alignof(type), allocator, SrcLoc()) type
#define PAW_NEW_SLICE(count, type) NewSlice<type>(count, nullptr, SrcLoc())
#define PAW_NEW_SLICE_IN(allocator, count, type) NewSlice<type>(count, allocator, SrcLoc())
#define PAW_NEW_SLICE_2D_IN(allocator, row_count, column_count, type) NewSlice2D<type>(row_count, column_count, allocator, SrcLoc())
#define PAW_NEW_SLICE_2D(row_count, column_count, type) PAW_NEW_SLICE_2D_IN(nullptr, row_count, column_count, type)
#define PAW_ALLOC_IN(allocator, size_bytes) AllocMem(size_bytes, 1, allocator, SrcLoc())
#define PAW_ALLOC(size_bytes) PAW_ALLOC_IN(nullptr, size_bytes)

#define PAW_DELETE(ptr) Delete(ptr, nullptr, SrcLoc())
#define PAW_DELETE_IN(allocator, ptr) Delete(ptr, allocator, SrcLoc())
#define PAW_DELETE_SLICE_IN(allocator, slice) DeleteSlice(slice, allocator, SrcLoc())
#define PAW_DELETE_SLICE(slice) PAW_DELETE_SLICE_IN(nullptr, slice)
#define PAW_DELETE_SLICE_2D_IN(allocator, slice) DeleteSlice2D(slice, allocator, SrcLoc())
#define PAW_DELETE_SLICE_2D(slice) PAW_DELETE_SLICE_IN(nullptr, slice)
#define PAW_FREE_IN(allocator, memory) FreeMem(memory, allocator, SrcLoc())
#define PAW_FREE(memory) PAW_FREE_IN(memory)

#pragma region implementation

inline void* operator new(size_t size, PtrSize alignment, IAllocator* allocator, SrcLocation src_loc) noexcept
{
	MemorySlice mem = AllocMem(size, alignment, allocator, src_loc);
	return mem.ptr;
}

inline void operator delete(void*, PtrSize, IAllocator*, SrcLocation) noexcept
{
	PAW_ASSERT(false, "operator delete shouldn't be called");
}

struct PlacementNewTag_t
{
};

constexpr void* operator new(PtrSize, void* ptr, PlacementNewTag_t) noexcept
{
	return ptr;
}

constexpr void operator delete(void*, void*, PlacementNewTag_t) noexcept
{
}

template <typename T>
void Delete(T* t, IAllocator* allocator, SrcLocation src)
{
	t->~T();
	FreeMem(MemorySlice{reinterpret_cast<Byte*>(t), sizeof(T)}, allocator, src);
}

template <typename T>
void Delete(T& t, IAllocator* allocator, SrcLocation src)
{
	Delete(&t, allocator, src);
}

template <typename T>
inline Slice<T> NewSlice(S32 count, IAllocator* allocator, SrcLocation src)
{
	MemorySlice mem = AllocMem(sizeof(T) * count, alignof(T), allocator, src);
	for (S32 i = 0; i < count; i++)
	{
		new (mem.ptr + i * sizeof(T), PlacementNewTag_t{}) T();
	}
	return Slice<T>{reinterpret_cast<T*>(mem.ptr), count};
}

template <typename T>
void DeleteSlice(Slice<T> slice, IAllocator* allocator, SrcLocation src)
{
	for (T& item : slice)
	{
		item.~T();
	}

	FreeMem(MemorySlice{reinterpret_cast<Byte*>(slice.items), CalcTotalSizeBytes(slice)}, allocator, src);
}

template <typename T>
inline Slice2D<T> NewSlice2D(S32 row_count, S32 column_count, IAllocator* allocator, SrcLocation src)
{
	S32 const count = row_count * column_count;
	MemorySlice mem = AllocMem(sizeof(T) * count, alignof(T), allocator, src);
	for (S32 i = 0; i < count; i++)
	{
		new (mem.ptr + i * sizeof(T), PlacementNewTag_t{}) T();
	}
	return Slice2D<T>{reinterpret_cast<T*>(mem.ptr), row_count, column_count};
}

template <typename T>
void DeleteSlice2D(Slice2D<T> slice, IAllocator* allocator, SrcLocation src)
{
	for (S32 row_index = 0; row_index < slice.row_count; row_index++)
	{
		for (S32 col_index = 0; col_index < slice.column_count; col_index++)
		{
			slice[row_index][col_index].~T();
		}
	}

	FreeMem(MemorySlice{reinterpret_cast<Byte*>(slice.items), CalcTotalSizeBytes(slice)}, allocator, src);
}

#pragma endregion