#pragma once

#include <core/std.h>

struct MemorySlice
{
	Byte* ptr = nullptr;
	PtrSize size_bytes = 0;
};

class IAllocator : NonCopyable
{
public:
	// Don't call these directly! Go through the PAW_NEW/DELETE tracking macros
	virtual MemorySlice Alloc(PtrSize size_Bytes, PtrSize alignment) = 0;
	virtual void Free(MemorySlice memory) = 0;

protected:
	IAllocator();
	virtual ~IAllocator();

	void AllocPages(PtrSize count);
	void FreePages(PtrSize count);
	void FreeAllPages();
	PtrSize CalcMemorySizeBytes();

	PtrSize GetPageCount() const;
	Byte* GetBaseAddress() const;

	static PtrSize GetPageSize();
	static PtrSize CalcPageCountFromSize(PtrSize size_bytes);

private:
	PtrSize page_count = 0;
	Byte* base_address = nullptr;
};
