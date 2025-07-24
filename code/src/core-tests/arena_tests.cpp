#include <testing/testing.h>

#include <core/arena.h>
#include <core/memory.inl>

#define PAW_TEST_MODULE_NAME Arena

PAW_TEST(FixedSizeArenaAllocator)
{
	static constexpr PtrSize buffer_size = 32;
	static Byte buffer[buffer_size]{};

	FixedSizeArenaAllocator allocator(buffer, buffer_size);
	PAW_TEST_EXPECT_EQUAL(allocator.GetFreeBytes(), buffer_size);

	ScopedDefaultAllocator allocator_scope{&allocator};

	Slice<S32> const ints = PAW_NEW_SLICE(4, S32);
	PAW_TEST_EXPECT_EQUAL(ints.count, 4);
	PAW_TEST_EXPECT_EQUAL(allocator.GetFreeBytes(), 16ull);

	ArenaMarker_t const marker = allocator.GetMarker();

	Slice<S32> const ints2 = PAW_NEW_SLICE(4, S32);
	PAW_TEST_EXPECT_EQUAL(ints2.count, 4);
	PAW_TEST_EXPECT_EQUAL(allocator.GetFreeBytes(), 0ull);

	allocator.FreeToMarker(marker);

	PAW_TEST_EXPECT_EQUAL(allocator.GetFreeBytes(), 16ull);

	allocator.FreeAll();

	PAW_TEST_EXPECT_EQUAL(allocator.GetFreeBytes(), allocator.GetTotalSizeBytes());
}

// PAW_TEST(InstrusiveArenaAllocator)
//{
//	InstrusiveArenaAllocator allocator{};
//	ScopedDefaultAllocator allocator_scope{&allocator};
//
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 0);
//
//	Slice<S32> const ints = PAW_NEW_SLICE(4, S32);
//	PAW_TEST_EXPECT_EQUAL(ints.count, 4);
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 1);
//
//	ArenaMarker_t const marker = allocator.GetMarker();
//
//	Slice<S32> const ints2 = PAW_NEW_SLICE(4, S32);
//	PAW_TEST_EXPECT_EQUAL(ints2.count, 4);
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 1);
//
//	allocator.FreeToMarker(marker);
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 1);
//
//	allocator.FreeAll();
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 0);
//
//	PAW_NEW_SLICE(10, Byte);
//	PAW_NEW_SLICE(allocator.GetFreeBytesInPage(), Byte);
//	PAW_NEW_SLICE(10, Byte);
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 2);
//
//	allocator.FreeAll();
//	PAW_TEST_EXPECT_EQUAL(allocator.CalcPageCount(), 0);
// }

PAW_TEST(PagedArenaAllocator)
{
	PagedArenaAllocator allocator{};
	ScopedDefaultAllocator allocator_scope{&allocator};

	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 0);

	Slice<S32> const ints = PAW_NEW_SLICE(4, S32);
	PAW_TEST_EXPECT_EQUAL(ints.count, 4);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 1);

	ArenaMarker_t const marker = allocator.GetMarker();

	Slice<S32> const ints2 = PAW_NEW_SLICE(4, S32);
	PAW_TEST_EXPECT_EQUAL(ints2.count, 4);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 1);

	allocator.FreeToMarker(marker);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 1);

	allocator.FreeAll();
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 0);

	PAW_NEW_SLICE(10, Byte);
	PAW_NEW_SLICE(allocator.GetFreeBytesInPage(), Byte);
	Slice<Byte> const alloc = PAW_NEW_SLICE(10, Byte);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 2);
	PAW_DELETE_SLICE(alloc);
	allocator.FreeAll();
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 0);
}

PAW_TEST(ArenaAllocator)
{
	ArenaAllocator allocator{};
	ScopedDefaultAllocator allocator_scope{&allocator};

	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 0);

	Slice<S32> const ints = PAW_NEW_SLICE(4, S32);
	PAW_TEST_EXPECT_EQUAL(ints.count, 4);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 1);

	// ArenaMarker_t const marker = allocator.GetMarker();

	Slice<S32> const ints2 = PAW_NEW_SLICE(4, S32);
	PAW_TEST_EXPECT_EQUAL(ints2.count, 4);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 1);

	// allocator.FreeToMarker(marker);
	// PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 1);

	allocator.FreeAll();
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 0);

	PAW_NEW_SLICE(10, Byte);
	// PAW_NEW_SLICE(allocator.GetFreeBytesInPage(), Byte);
	// #TODO: Make this proper
	PAW_NEW_SLICE(KiloBytes(64) - 10, Byte);
	PAW_NEW_SLICE(KiloBytes(160), Byte);
	Slice<Byte> const alloc = PAW_NEW_SLICE(10, Byte);
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 4);
	PAW_DELETE_SLICE(alloc);
	allocator.FreeAll();
	PAW_TEST_EXPECT_EQUAL(allocator.GetPageCount(), 0);
}