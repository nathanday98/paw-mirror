#pragma once

#include <core/arena_types.h>
#include <core/memory_types.h>
#include <core/slice_types.h>
#include <core/assert.h>

struct ArenaMarker_t
{
	PtrSize head;
};

class FixedSizeArenaAllocator final : public IAllocator
{
public:
	FixedSizeArenaAllocator() = default;
	FixedSizeArenaAllocator(Byte* memory, PtrSize size_Bytes);

	void InitFromMemory(Byte* memory, PtrSize size_bytes);

	MemorySlice Alloc(PtrSize size_Bytes, PtrSize alignment) override;
	void Free(MemorySlice memory) override;

	void FreeAll();

	void FreeToMarker(ArenaMarker_t marker);
	ArenaMarker_t GetMarker() const;
	PtrSize GetFreeBytes() const;
	PtrSize GetTotalSizeBytes() const;
	Byte* GetBasePtr() const;

private:
	Byte* memory = nullptr;
	PtrSize total_size_Bytes = 0;
	PtrSize head_Bytes = 0;
};

#if 0
class InstrusiveArenaAllocator final : public IAllocator
{
public:
	InstrusiveArenaAllocator();
	~InstrusiveArenaAllocator();

	MemorySlice Alloc(PtrSize size_Bytes, PtrSize alignment) override;
	void Free(MemorySlice memory) override;

	void FreeAll();

	void FreeToMarker(ArenaMarker_t marker);
	ArenaMarker_t GetMarker() const;
	S32 CalcPageCount() const;
	PtrSize GetFreeBytesInPage() const;

private:
	struct PageHeader;

	PageHeader* current_page;
};
#endif

class PagedArenaAllocator final : public IAllocator
{
public:
	PagedArenaAllocator();
	~PagedArenaAllocator();

	MemorySlice Alloc(PtrSize size_Bytes, PtrSize alignment) override;
	void Free(MemorySlice memory) override;

	void FreeAll();

	void FreeToMarker(ArenaMarker_t marker);
	ArenaMarker_t GetMarker() const;
	S32 GetPageCount() const;
	PtrSize GetFreeBytesInPage() const;

private:
	struct Page
	{
		MemorySlice memory;
		PtrSize used = 0;
	};

	Page const& GetCurrentPage() const;
	Page& GetCurrentPage();

	static constexpr S32 max_page_count = 32;

	Page pages[max_page_count]{};
};

class ArenaAllocator final : public IAllocator
{
public:
	ArenaAllocator();
	~ArenaAllocator();

	MemorySlice Alloc(PtrSize size_Bytes, PtrSize alignment) override;
	void Free(MemorySlice memory) override;

	void FreeAll();

	void FreeToMarker(ArenaMarker_t marker);
	ArenaMarker_t GetMarker() const;

	S32 GetPageCount() const;

private:
	PtrSize used_Bytes = 0;
};