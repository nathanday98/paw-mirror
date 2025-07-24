#include <core/arena.h>

#include <core/memory.h>

FixedSizeArenaAllocator::FixedSizeArenaAllocator(Byte* memory, PtrSize size_Bytes)
	: memory(memory)
	, total_size_Bytes(size_Bytes)
{
}

void FixedSizeArenaAllocator::InitFromMemory(Byte* in_memory, PtrSize size_bytes)
{
	head_Bytes = 0;
	memory = in_memory;
	total_size_Bytes = size_bytes;
}

MemorySlice FixedSizeArenaAllocator::Alloc(PtrSize size_Bytes, PtrSize alignment)
{
	PtrSize const alignment_offset = CalcAlignmentOffset(memory + head_Bytes, alignment);
	PtrSize const total_size = size_Bytes + alignment_offset;
	PAW_ASSERT(head_Bytes + total_size <= total_size_Bytes, "Not enough space in the arena. Consider increasing the size or using a dynamic one");

	MemorySlice const result{
		.ptr = memory + head_Bytes + alignment_offset,
		.size_bytes = size_Bytes,
	};

	head_Bytes += total_size;

	return result;
}

void FixedSizeArenaAllocator::Free(MemorySlice in_memory)
{
	PAW_ASSERT(in_memory.ptr >= memory && in_memory.ptr + in_memory.size_bytes <= memory + total_size_Bytes, "Memory is not in the memory range owned by this allocator, did you pass the right one in?");
}

void FixedSizeArenaAllocator::FreeAll()
{
	head_Bytes = 0;
}

ArenaMarker_t FixedSizeArenaAllocator::GetMarker() const
{
	return {head_Bytes};
}

void FixedSizeArenaAllocator::FreeToMarker(ArenaMarker_t marker)
{
	PAW_ASSERT(marker.head <= total_size_Bytes, "Arena marker is not in a valid range");
	PAW_ASSERT(marker.head <= head_Bytes, "Arena marker is not less than the head");
	head_Bytes = marker.head;
}

PtrSize FixedSizeArenaAllocator::GetFreeBytes() const
{
	return total_size_Bytes - head_Bytes;
}

PtrSize FixedSizeArenaAllocator::GetTotalSizeBytes() const
{
	return total_size_Bytes;
}

Byte* FixedSizeArenaAllocator::GetBasePtr() const
{
	return memory;
}

#if 0
struct InstrusiveArenaAllocator::PageHeader
{
	PageHeader* prev;
	PtrSize used;
	PtrSize size_Bytes;

	Byte* GetCurrentPtr()
	{
		return reinterpret_cast<Byte*>(this) + used;
	}

	MemorySlice GetSlice()
	{
		return {reinterpret_cast<Byte*>(this), size_Bytes};
	}

	PtrSize CalcBytesRemaining() const
	{
		return size_Bytes - used;
	}
};


InstrusiveArenaAllocator::InstrusiveArenaAllocator()
{
	current_page = nullptr;
}

InstrusiveArenaAllocator::~InstrusiveArenaAllocator()
{
	FreeAll();
}

MemorySlice InstrusiveArenaAllocator::Alloc(PtrSize size_Bytes, PtrSize alignment)
{
	bool space_available = false;

	Byte* start_ptr = nullptr;
	PtrSize alignment_offset = 0;

	if (current_page)
	{
		PtrSize const remaining = current_page->CalcBytesRemaining();
		start_ptr = current_page->GetCurrentPtr();
		alignment_offset = CalcAlignmentOffset(start_ptr, alignment);

		space_available = size_Bytes + alignment_offset <= remaining;
	}

	if (!space_available || current_page == nullptr)
	{
		MemorySlice mem = PlatformPageAlloc(platform_id, 1);
		PageHeader* new_page = reinterpret_cast<PageHeader*>(mem.ptr);
		new_page->used = sizeof(PageHeader);
		new_page->size_Bytes = mem.size_bytes;
		PAW_ASSERT(size_Bytes <= new_page->CalcBytesRemaining(), "Impossible to fit allocation inside page");
		new_page->prev = current_page;
		current_page = new_page;

		start_ptr = current_page->GetCurrentPtr();
		alignment_offset = CalcAlignmentOffset(start_ptr, alignment);
	}

	PAW_ASSERT(start_ptr, "No memory to write to");

	current_page->used += size_Bytes + alignment_offset;

	return {start_ptr + alignment_offset, size_Bytes};
}

void InstrusiveArenaAllocator::Free(MemorySlice /*memory*/)
{
	// #TODO: I could keep track of the last allocation here (unaligned and unaligned offset)
	// And if it matches, I could reset to before
}

void InstrusiveArenaAllocator::FreeAll()
{
	PageHeader* page = current_page;
	while (page)
	{
		PageHeader* prev_page = page->prev;
		PlatformPageFree(platform_id, page->GetSlice());
		page = prev_page;
	}

	current_page = nullptr;
}

void InstrusiveArenaAllocator::FreeToMarker(ArenaMarker_t marker)
{
	if (marker.head == 0)
	{
		FreeAll();
		return;
	}

	while (current_page)
	{
		U64 const page_start = reinterpret_cast<U64>(current_page);
		U64 const page_end = page_start + current_page->used;
		if (marker.head >= page_start && marker.head <= page_end)
		{
			current_page->used = marker.head - page_start;
			return;
		}

		PageHeader* old_page = current_page;
		current_page = old_page->prev;
		PlatformPageFree(platform_id, old_page->GetSlice());
	}

	PAW_UNREACHABLE;
}

ArenaMarker_t InstrusiveArenaAllocator::GetMarker() const
{
	if (current_page)
	{
		return ArenaMarker_t{reinterpret_cast<PtrSize>(current_page->GetCurrentPtr())};
	}

	return ArenaMarker_t{0};
}

S32 InstrusiveArenaAllocator::CalcPageCount() const
{
	S32 result = 0;
	for (PageHeader* page = current_page; page; page = page->prev)
	{
		result++;
	}
	return result;
}

PtrSize InstrusiveArenaAllocator::GetFreeBytesInPage() const
{
	if (current_page)
	{
		return current_page->CalcBytesRemaining();
	}

	return 0;
}
#endif

PagedArenaAllocator::PagedArenaAllocator()
{
}

PagedArenaAllocator::~PagedArenaAllocator()
{
}

MemorySlice PagedArenaAllocator::Alloc(PtrSize size_Bytes, PtrSize alignment)
{
	bool space_available = false;

	Byte* start_ptr = nullptr;
	PtrSize alignment_offset = 0;

	if (GetPageCount() > 0)
	{
		Page& current_page = GetCurrentPage();
		PtrSize const remaining = current_page.memory.size_bytes - current_page.used;
		start_ptr = current_page.memory.ptr + current_page.used;
		alignment_offset = CalcAlignmentOffset(start_ptr, alignment);

		space_available = size_Bytes + alignment_offset <= remaining;
	}

	if (!space_available || GetPageCount() == 0)
	{
		Byte* const new_page_ptr = GetBaseAddress() + CalcMemorySizeBytes();
		PAW_ASSERT(GetPageCount() < max_page_count - 1, "Not enough pages left in the arena");
		Page& new_page = pages[GetPageCount()];
		AllocPages(1);
		new_page.used = 0;
		new_page.memory = {new_page_ptr, GetPageSize()};
		PAW_ASSERT(size_Bytes <= new_page.memory.size_bytes, "Impossible to fit allocation inside page");

		start_ptr = new_page.memory.ptr;
		alignment_offset = CalcAlignmentOffset(start_ptr, alignment);
	}

	PAW_ASSERT(start_ptr, "No memory to write to");

	Page& current_page = GetCurrentPage();
	current_page.used += size_Bytes + alignment_offset;

	return {start_ptr + alignment_offset, size_Bytes};
}

void PagedArenaAllocator::Free(MemorySlice /*memory*/)
{
	// #TODO: I could keep track of the last allocation here (unaligned and unaligned offset)
	// And if it matches, I could reset to before
}

void PagedArenaAllocator::FreeAll()
{
	for (S32 i = 0; i < GetPageCount(); i++)
	{
		Page& page = pages[i];
		page.memory.ptr = nullptr;
		page.memory.size_bytes = 0;
	}
	FreeAllPages();
}

void PagedArenaAllocator::FreeToMarker(ArenaMarker_t marker)
{
	if (marker.head == 0)
	{
		FreeAll();
		return;
	}

	for (S32 i = GetPageCount() - 1; i >= 0; i--)
	{
		Page& page = pages[i];
		PtrSize const page_start = reinterpret_cast<PtrSize>(page.memory.ptr);
		PtrSize const page_end = page_start + page.used;
		if (marker.head >= page_start && marker.head <= page_end)
		{
			page.used = marker.head - page_start;
			return;
		}

		FreePages(1);
	}

	// Marker was not in the range of any page
	PAW_UNREACHABLE;
}

ArenaMarker_t PagedArenaAllocator::GetMarker() const
{
	if (GetPageCount() > 0)
	{
		Page const& page = GetCurrentPage();
		return ArenaMarker_t{reinterpret_cast<PtrSize>(page.memory.ptr + page.used)};
	}
	return ArenaMarker_t{0};
}

S32 PagedArenaAllocator::GetPageCount() const
{
	return static_cast<S32>(IAllocator::GetPageCount());
}

PtrSize PagedArenaAllocator::GetFreeBytesInPage() const
{
	if (GetPageCount() > 0)
	{
		Page const& page = GetCurrentPage();
		return page.memory.size_bytes - page.used;
	}
	return 0;
}

PagedArenaAllocator::Page& PagedArenaAllocator::GetCurrentPage()
{
	PAW_ASSERT(GetPageCount() > 0, "This arena has no pages");
	return pages[GetPageCount() - 1];
}

PagedArenaAllocator::Page const& PagedArenaAllocator::GetCurrentPage() const
{
	PAW_ASSERT(GetPageCount() > 0, "This arena has no pages");
	return pages[GetPageCount() - 1];
}

ArenaAllocator::ArenaAllocator()
{
}

ArenaAllocator::~ArenaAllocator()
{
}

MemorySlice ArenaAllocator::Alloc(PtrSize size_Bytes, PtrSize alignment)
{
	PtrSize const remaining = CalcMemorySizeBytes() - used_Bytes;
	Byte* const start_ptr = GetBaseAddress() + used_Bytes;
	PtrSize const alignment_offset = CalcAlignmentOffset(start_ptr, alignment);
	PtrSize const total_size_Bytes = size_Bytes + alignment_offset;
	if (total_size_Bytes > remaining)
	{
		PtrSize const overflow_Bytes = total_size_Bytes - remaining;
		PtrSize const new_page_count = CalcPageCountFromSize(overflow_Bytes);
		AllocPages(new_page_count);
	}

	used_Bytes += total_size_Bytes;

	return {start_ptr + alignment_offset, size_Bytes};
}

void ArenaAllocator::Free(MemorySlice /*memory*/)
{
}

void ArenaAllocator::FreeAll()
{
	FreeAllPages();
	used_Bytes = 0;
}

void ArenaAllocator::FreeToMarker(ArenaMarker_t marker)
{
	PAW_UNUSED_ARG(marker);
}

ArenaMarker_t ArenaAllocator::GetMarker() const
{
	return {used_Bytes};
}

S32 ArenaAllocator::GetPageCount() const
{
	return static_cast<S32>(IAllocator::GetPageCount());
}
