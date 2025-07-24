#pragma once

#include <core/memory_types.h>
#include <core/src_location_types.h>

void MemoryInit();
void MemoryDeinit();

MemorySlice AllocMem(PtrSize size, PtrSize alignment, IAllocator* allocator, SrcLocation src);
void FreeMem(MemorySlice slice, IAllocator* allocator, SrcLocation src);

PtrSize CalcAlignmentOffset(Byte* ptr, PtrSize alignment);
Byte* AlignPointerForward(Byte* ptr, PtrSize alignment);
PtrSize AlignSizeForward(PtrSize size, PtrSize alignemtn);
bool IsPointerAligned(Byte const* pointer, PtrSize alignment);

class ScopedDefaultAllocator
{
public:
	ScopedDefaultAllocator(IAllocator* allocator);
	~ScopedDefaultAllocator();

private:
	IAllocator* const previous_allocator;
};

constexpr inline PtrSize KiloBytes(PtrSize num)
{
	return num * 1024LL;
}

constexpr inline PtrSize MegaBytes(PtrSize num)
{
	return KiloBytes(num) * 1024LL;
}

constexpr inline PtrSize GigaBytes(PtrSize num)
{
	return MegaBytes(num) * 1024LL;
}
