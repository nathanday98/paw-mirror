#include <core/tlsf.h>

#include <core/math.h>
#include <core/memory.inl>

#include <new>
#include <cstdio>

enum BlockFlags
{
	BlockFlags_None,
	BlockFlags_Free = 1 << 0,
	// BlockFlags_LastPhysical = 1 << 1,
};

struct BlockInfo
{
	U64 data;

	BlockInfo(U64 size, bool free /*, bool is_last_physical*/)
	{
		data = size;
		if (free)
		{
			data |= BlockFlags_Free;
		}
		// if (is_last_physical)
		//{
		//	data |= BlockFlags_LastPhysical;
		// }
	}

	void SetSize(U64 size)
	{
		PAW_ASSERT((size & 0b11) == 0, "Size is not aligned properly");
		data = size | (data & 0b11);
	}

	void SetUsed()
	{
		data = data & ~(BlockFlags_Free);
	}

	// void SetNotLastPhysical()
	//{
	//	data = data & ~(BlockFlags_LastPhysical);
	// }

	// void SetLastPhysical()
	//{
	//	data = data | BlockFlags_LastPhysical;
	// }

	U64 GetSize() const
	{
		return data & 0xFFFFFFFFFFFFFFFC;
	}

	bool IsFree() const
	{
		return (data & BlockFlags_Free) == BlockFlags_Free;
	}

	// bool IsLastPhysical() const
	//{
	//	return (data & BlockFlags_LastPhysical) == BlockFlags_LastPhysical;
	// }
};

struct TLSFAllocator::CommonBlock
{
	CommonBlock(BlockInfo info, U64 prev_physical_block)
		: info(info)
		, prev_physical_block(prev_physical_block)
	{
	}

	BlockInfo info;
	U64 prev_physical_block;
};

struct TLSFAllocator::FreeBlock : public CommonBlock
{
	FreeBlock(BlockInfo info, U64 prev_physical_block, FreeBlock* next_free_block, FreeBlock* prev_free_block)
		: CommonBlock(info, prev_physical_block)
		, next_free_block(next_free_block)
		, prev_free_block(prev_free_block)
	{
	}

	FreeBlock* next_free_block;
	FreeBlock* prev_free_block;
};

struct UsedBlock : TLSFAllocator::CommonBlock
{
	UsedBlock(TLSFAllocator::FreeBlock const& free_block)
		: CommonBlock(BlockInfo(free_block.info.GetSize(), false /*, free_block.info.IsLastPhysical()*/), free_block.prev_physical_block)
	{
	}
};

struct Index
{
	S32 top_level;
	S32 second_level;
};

static Index Map(PtrSize size_bytes)
{
	S32 const top_bin_index = BitScanMSB(size_bytes);
	S32 const sub_bin_index = ((size_bytes ^ (1 << top_bin_index)) >> (top_bin_index - TLSFAllocator::second_level_index));

	return {top_bin_index, sub_bin_index};
}

// #TODO: Look into grouping a small amount of buckets at the beginning together
// https://ricefields.me/2024/04/20/tlsf-allocator.html
TLSFAllocator::TLSFAllocator()
{
}

TLSFAllocator::~TLSFAllocator()
{
}

MemorySlice TLSFAllocator::Alloc(PtrSize size_bytes, PtrSize alignment)
{
	PAW_ASSERT(alignment <= 256, "alignment more than 256 bytes is not supported");

	PtrSize const total_size_bytes = size_bytes + sizeof(UsedBlock) + alignment;
	PAW_ASSERT(total_size_bytes <= GetPageSize(), "TLSF doesn't support allocations greater than platform page size");
	Index index = Map(total_size_bytes);
	// Mask out all the bits below the sub bin index
	U32 sub_bin_bitmap = second_level_bitmaps[index.top_level] & (~0u << index.second_level);

	if (sub_bin_bitmap == 0)
	{
		//	Mask out all the bit for the top bin index and all below it
		U64 bin_bitmap = top_level_bitmap & (~0u << (index.top_level + 1));

		// Out of memory
		if (bin_bitmap == 0)
		{
			Byte* const new_page_ptr = GetBaseAddress() + CalcMemorySizeBytes();
			AllocPages(1);
			// #TODO: Change page size here to actual new size if we need to support allocs > page size
			MemorySlice new_block_memory = {new_page_ptr, GetPageSize()};
			U64 prev_physical_block = 0;
			if (last_physical_block)
			{
				if (last_physical_block->info.IsFree())
				{
					FreeBlock* const free_block = std::launder(reinterpret_cast<FreeBlock*>(last_physical_block));
					RemoveBlock(free_block);
					new_block_memory.ptr = reinterpret_cast<Byte*>(free_block);
					// #TODO: Change page size here to actual new size if we need to support allocs > page size
					new_block_memory.size_bytes = GetPageSize() + free_block->info.GetSize();
					prev_physical_block = free_block->prev_physical_block;
				}
				else
				{
					prev_physical_block = last_physical_block->info.GetSize();
				}
			}
			InsertBlock(new_block_memory, prev_physical_block);

			// NOTE(nathan): Don't do the +1 here. We just added new memory which means we could find memory in
			// a matching bin now
			bin_bitmap = top_level_bitmap & (~0u << index.top_level);
		}

		PAW_ASSERT(bin_bitmap != 0, "Viable bin was not found... It should have found one");
		index.top_level = BitScanLSB(bin_bitmap);
		sub_bin_bitmap = second_level_bitmaps[index.top_level];
	}

	index.second_level = BitScanLSB(sub_bin_bitmap);

	S32 const free_list_index = index.top_level * sub_bin_count + index.second_level;
	FreeBlock*& head = second_level_pointers[free_list_index];
	PAW_ASSERT(head != nullptr, "Free list should not be nullptr if the bitmap isn't empty");
	FreeBlock* block = head;
	head = block->next_free_block;
	if (head == nullptr)
	{
		second_level_bitmaps[index.top_level] &= ~(1 << index.second_level);
		if (second_level_bitmaps[index.top_level] == 0)
		{
			top_level_bitmap &= ~(1 << index.top_level);
		}
	}

	PtrSize const aligned_block_size = AlignSizeForward(total_size_bytes, 4);
	{

		if (block->info.GetSize() > aligned_block_size + sizeof(FreeBlock))
		{
			PtrSize const new_split_size = block->info.GetSize() - aligned_block_size;
			Byte* const new_split_ptr = reinterpret_cast<Byte*>(block) + aligned_block_size;
			block->info.SetSize(aligned_block_size);
			// block->info.SetNotLastPhysical();
			InsertBlock({new_split_ptr, new_split_size}, aligned_block_size);
		}
	}

	UsedBlock* const used_block = new (block) UsedBlock(*block);

	Byte* result_ptr = reinterpret_cast<Byte*>(used_block) + sizeof(UsedBlock);
	PtrSize alignment_offset = CalcAlignmentOffset(result_ptr, alignment);
	if (alignment_offset == 0)
	{
		alignment_offset = alignment;
	}
	result_ptr += alignment_offset;
	result_ptr[-1] = static_cast<Byte>(alignment_offset & 0xFF);

	return {result_ptr, size_bytes};
}

void TLSFAllocator::Free(MemorySlice in_memory)
{
	PtrDiff shift = in_memory.ptr[-1];
	if (shift == 0)
	{
		shift = 256;
	}

	Byte* const actual_address = in_memory.ptr - shift;
	Byte* const block_address = actual_address - sizeof(UsedBlock);
	UsedBlock* block = std::launder(reinterpret_cast<UsedBlock*>(block_address));
	if (block != last_physical_block)
	{
		Byte* const next_block_address = block_address + block->info.GetSize();
		CommonBlock const* const next_block_common = std::launder(reinterpret_cast<CommonBlock*>(next_block_address));
		if (next_block_common->info.IsFree())
		{
			FreeBlock const* const next_block = std::launder(reinterpret_cast<FreeBlock*>(next_block_address));
			if (next_block == last_physical_block)
			{
				// block->info.SetLastPhysical();
				last_physical_block = block;
			}
			else
			{
				CommonBlock* const next_next_block_common = std::launder(reinterpret_cast<CommonBlock*>(next_block_address + next_block->info.GetSize()));
				next_next_block_common->prev_physical_block += block->info.GetSize();
			}
			block->info.SetSize(block->info.GetSize() + next_block->info.GetSize());

			RemoveBlock(next_block);
		}
	}
	Byte* const prev_block_address = block_address - block->prev_physical_block;
	CommonBlock const* const prev_block_common = std::launder(reinterpret_cast<CommonBlock*>(prev_block_address));
	if (prev_block_common->info.IsFree())
	{
		FreeBlock* const prev_block = std::launder(reinterpret_cast<FreeBlock*>(prev_block_address));

		RemoveBlock(prev_block);

		PtrSize const new_block_size = prev_block->info.GetSize() + block->info.GetSize();
		prev_block->info.SetSize(new_block_size);
		if (block == last_physical_block)
		{
			// prev_block->info.SetLastPhysical();
			last_physical_block = prev_block;
		}
		else
		{
			CommonBlock* const next_block_common = std::launder(reinterpret_cast<CommonBlock*>(prev_block_address + new_block_size));
			next_block_common->prev_physical_block = new_block_size;
		}
		InsertBlock({prev_block_address, new_block_size}, prev_block->prev_physical_block);
	}
	else
	{
		InsertBlock({block_address, block->info.GetSize()}, block->prev_physical_block);
	}
}

void TLSFAllocator::InsertBlock(MemorySlice block, U64 prev_physical_block)
{
	// PAW_ASSERT(IsPointerAligned(block.ptr, sizeof(FreeBlock)), "Memory block pointer is not aligned");
	PAW_ASSERT(block.size_bytes >= sizeof(FreeBlock), "Memory block size is not big enough for FreeBlock");
	PAW_ASSERT(IsPointerAligned(block.ptr, 4), "Memory block pointer is not aligned");

	PAW_ASSERT(block.size_bytes >= sizeof(FreeBlock), "Memory block is not big enough to contain tracking data (FreeBlock)");
	Index const index = Map(block.size_bytes);
	S32 const free_list_index = index.top_level * sub_bin_count + index.second_level;
	FreeBlock*& head = second_level_pointers[free_list_index];
	BlockInfo const block_info = BlockInfo(block.size_bytes, true);
	FreeBlock* new_block = new (block.ptr) FreeBlock(block_info, prev_physical_block, head, nullptr);
	if (block.ptr + block.size_bytes == GetBaseAddress() + CalcMemorySizeBytes())
	{
		last_physical_block = new_block;
	}
	if (head != nullptr)
	{
		head->prev_free_block = new_block;
	}
	head = new_block;
	top_level_bitmap |= 1 << index.top_level;
	second_level_bitmaps[index.top_level] |= 1 << index.second_level;
}

void TLSFAllocator::RemoveBlock(FreeBlock const* const block)
{
	if (block->prev_free_block != nullptr)
	{
		block->prev_free_block->next_free_block = block->next_free_block;
	}
	else
	{
		Index const index = Map(block->info.GetSize());
		U32 const sub_bin_bitmap = second_level_bitmaps[index.top_level] & (~0u << index.second_level);
		PAW_ASSERT(sub_bin_bitmap != 0, "This block exists and is free, so this should not be 0");
		S32 const free_list_index = index.top_level * sub_bin_count + index.second_level;
		FreeBlock*& head = second_level_pointers[free_list_index];
		PAW_ASSERT(head == block, "next_block should be the head of the free list");
		head = block->next_free_block;
		if (head == nullptr)
		{
			second_level_bitmaps[index.top_level] &= ~(1 << index.second_level);
			if (second_level_bitmaps[index.top_level] == 0)
			{
				top_level_bitmap &= ~(1 << index.top_level);
			}
		}
	}

	if (block->next_free_block != nullptr)
	{
		block->next_free_block->prev_free_block = block->prev_free_block;
	}
}

void TLSFAllocator::Print()
{
	std::printf("========================================================================\n");
	Byte* ptr = GetBaseAddress();
	while (true)
	{
		CommonBlock const* const block = std::launder(reinterpret_cast<CommonBlock*>(ptr));
		bool const free = block->info.IsFree();
		if (free)
		{
			FreeBlock const* const free_block = std::launder(reinterpret_cast<FreeBlock*>(ptr));
			std::printf("Block: %s - %llu - next: %p, prev: %p\n", free ? "Free" : "Used", block->info.GetSize(), (void*)free_block->next_free_block, (void*)free_block->prev_free_block);
		}
		else
		{
			std::printf("Block: %s - %llu\n", free ? "Free" : "Used", block->info.GetSize());
		}
		if (block == last_physical_block)
		{
			break;
		}
		ptr += block->info.GetSize();
	}
	std::printf("========================================================================\n");
}
