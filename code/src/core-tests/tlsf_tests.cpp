#include <core/tlsf.h>
#include <core/memory.inl>
#include <core/slice.inl>

#include <testing/testing.h>

struct alignas(16) Thing
{
};

PAW_TEST(Alloc)
{
	TLSFAllocator allocator{};
	ScopedDefaultAllocator default_allocator{&allocator};
	// allocator.Print();
	allocator.Alloc(KiloBytes(64) - 17, 1);
	Slice<Byte> bytes = PAW_NEW_SLICE(100, Byte);
	allocator.Print();
	Slice<Byte> bytes2 = PAW_NEW_SLICE(100, Byte);
	allocator.Print();
	Thing* thing = PAW_NEW(Thing)();
	allocator.Print();
	PAW_DELETE_SLICE(bytes);
	allocator.Print();
	PAW_DELETE_SLICE(bytes2);
	allocator.Print();
	PAW_DELETE(thing);
	allocator.Print();

	Slice<Byte> bytes3 = PAW_NEW_SLICE(100, Byte);
	(void)bytes3;
	allocator.Print();
}