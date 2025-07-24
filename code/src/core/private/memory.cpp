#include <core/memory.h>

#include <core/assert.h>
#include <core/platform.h>

union AllocatorSlot
{
	IAllocator* allocator;
	S32 next_free_slot_index;
};

static constexpr S32 g_max_allocators = 256;
static AllocatorSlot g_allocators[g_max_allocators]{};
static S32 g_first_free_allocator_index = -1;

static constexpr PtrSize g_page_size_bytes = KiloBytes(64);
static constexpr PtrSize g_page_shift_count = 64 - __builtin_clzll(g_page_size_bytes) - 1;
static constexpr PtrSize g_address_space_per_allocator_Bytes = GigaBytes(64);
static constexpr PtrSize g_allocator_shift_count = 64 - __builtin_clzll(g_address_space_per_allocator_Bytes) - 1;
static constexpr PtrSize g_page_count_per_allocator = g_address_space_per_allocator_Bytes / g_page_size_bytes;

static Byte* g_base_address = nullptr;

static PtrSize CalcAllocatorIndex(Byte* ptr)
{
	PtrSize const offset = reinterpret_cast<PtrSize>(ptr) - reinterpret_cast<PtrSize>(g_base_address);
	PtrSize const allocator_index = offset >> g_allocator_shift_count;
	return allocator_index;
}

static Byte* RegisterAllocator(IAllocator* allocator)
{
	PAW_ASSERT(g_first_free_allocator_index != -1, "Run out of allocator spaces");
	PAW_ASSERT(g_base_address, "Initial address space is not allocated yet. Are you creating an allocator before the memory subsystem init");

	S32 const index = g_first_free_allocator_index;
	AllocatorSlot& slot = g_allocators[index];
	g_first_free_allocator_index = slot.next_free_slot_index;

	Byte* const base_address = g_base_address + static_cast<PtrSize>(index) * g_address_space_per_allocator_Bytes;

	slot.allocator = allocator;

	return base_address;
}

void MemoryInit()
{
	PtrSize const address_space_bytes = static_cast<PtrSize>(g_max_allocators) * GigaBytes(64);

	for (S32 i = 0; i < g_max_allocators - 1; i++)
	{
		AllocatorSlot& slot = g_allocators[i];
		slot.next_free_slot_index = i + 1;
	}

	g_allocators[g_max_allocators - 1].next_free_slot_index = -1;
	g_first_free_allocator_index = 0;

	Byte* const address_space = PlatformReserveAddressSpace(address_space_bytes);
	g_base_address = address_space;
}

void MemoryDeinit()
{
}

IAllocator::IAllocator()
	: base_address(RegisterAllocator(this))
{
}

IAllocator::~IAllocator()
{
	FreeAllPages();

	PtrSize const allocator_index = CalcAllocatorIndex(base_address);
	AllocatorSlot& slot = g_allocators[allocator_index];
	slot.next_free_slot_index = g_first_free_allocator_index;
	g_first_free_allocator_index = allocator_index;
}

void IAllocator::AllocPages(PtrSize count)
{
	PAW_ASSERT(page_count < g_page_count_per_allocator, "Reached maximum page count per allocator");

	Byte* const new_page_address = base_address + page_count * g_page_size_bytes;
	PlatformCommitAddressSpace(new_page_address, g_page_size_bytes * count);
	page_count += count;
}

void IAllocator::FreePages(PtrSize shrink_count)
{
	PAW_ASSERT(page_count >= shrink_count, "You are requesting to free more pages than the allocator owns");

	PtrSize const free_Bytes = shrink_count * g_page_size_bytes;
	PtrSize const start_offset_Bytes = (page_count - shrink_count) * g_page_size_bytes;
	Byte* const free_ptr = base_address + start_offset_Bytes;

	PlatformDecommitAddressSpace(free_ptr, free_Bytes);

	page_count -= shrink_count;
}

void IAllocator::FreeAllPages()
{
	PlatformDecommitAddressSpace(base_address, page_count * g_page_size_bytes);
	page_count = 0;
}

PtrSize IAllocator::CalcMemorySizeBytes()
{
	return page_count * g_page_size_bytes;
}

PtrSize IAllocator::GetPageCount() const
{
	return page_count;
}

Byte* IAllocator::GetBaseAddress() const
{
	return base_address;
}

PtrSize IAllocator::GetPageSize()
{
	return g_page_size_bytes;
}

PtrSize IAllocator::CalcPageCountFromSize(PtrSize size_bytes)
{
	PtrSize const page_count = ((size_bytes + g_page_size_bytes) >> g_page_shift_count);
	return page_count;
}

static thread_local IAllocator* g_default_allocator = nullptr;

MemorySlice AllocMem(PtrSize size, PtrSize alignment, IAllocator* allocator, SrcLocation /*src*/)
{
	IAllocator* allocator_to_use = allocator ? allocator : g_default_allocator;
	PAW_ASSERT(allocator_to_use, "There is no active allocator to use, either pass one in or set an allocator scope");
	return allocator_to_use->Alloc(size, alignment);
}

void FreeMem(MemorySlice slice, IAllocator* allocator, SrcLocation /*src*/)
{
	PtrSize const allocator_index = CalcAllocatorIndex(slice.ptr);
	PAW_ASSERT(allocator_index < g_max_allocators, "Allocator index not in range");
	IAllocator* allocator_to_use = allocator ? allocator : g_allocators[allocator_index].allocator;
	PAW_ASSERT(allocator_to_use, "There is no active allocator to use, either pass one in or set an allocator scope");
	allocator_to_use->Free(slice);
}

PtrSize CalcAlignmentOffset(Byte* ptr, PtrSize alignment)
{
	U64 alignment_offset = 0;
	U64 alignment_mask = alignment - 1;
	U64 result_ptr = (U64)ptr;
	U64 mask = result_ptr & alignment_mask;
	alignment_offset = (mask > 0) * (alignment - (result_ptr & alignment_mask));
	return alignment_offset;
}

Byte* AlignPointerForward(Byte* ptr, PtrSize alignment)
{
	PAW_ASSERT((alignment & (alignment - 1)) == 0, "alignment needs to be a power of 2");

	return ptr + CalcAlignmentOffset(ptr, alignment);
}

PtrSize AlignSizeForward(PtrSize size, PtrSize alignment)
{
	PtrSize const mask = alignment - 1;
	PAW_ASSERT((alignment & mask) == 0, "alignment needs to be a power of 2");

	return (size + mask) & (~mask);
}

bool IsPointerAligned(Byte const* pointer, PtrSize alignment)
{
	PtrSize const alignment_mask = alignment - 1;
	return (reinterpret_cast<PtrSize>(pointer) & alignment_mask) == 0;
}

ScopedDefaultAllocator::ScopedDefaultAllocator(IAllocator* allocator)
	: previous_allocator(g_default_allocator)
{
	g_default_allocator = allocator;
}

ScopedDefaultAllocator::~ScopedDefaultAllocator()
{
	g_default_allocator = previous_allocator;
}
