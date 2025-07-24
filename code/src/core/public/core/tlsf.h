#pragma once

#include <core/memory_types.h>

class TLSFAllocator : public IAllocator
{
public:
	TLSFAllocator();
	~TLSFAllocator();

	MemorySlice Alloc(PtrSize size_bytes, PtrSize alignment) override;
	void Free(MemorySlice memory) override;

	void Print();

	struct CommonBlock;
	struct FreeBlock;

	static constexpr S32 second_level_index = 5; // log2(sub_bin_count);
private:
	void InsertBlock(MemorySlice block, U64 prev_physical_block);
	void RemoveBlock(FreeBlock const* const block);

	static constexpr S32 top_level_count = 63;
	static constexpr S32 sub_bin_count = 1 << second_level_index;
	static constexpr S32 second_level_count = top_level_count * sub_bin_count;

	U64 top_level_bitmap = 0;
	U32 second_level_bitmaps[top_level_count]{};
	FreeBlock* second_level_pointers[second_level_count]{};
	CommonBlock* last_physical_block = nullptr;

	static_assert(sizeof(second_level_bitmaps[0]) * 8 == sub_bin_count);
};