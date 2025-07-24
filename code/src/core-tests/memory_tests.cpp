#include <core/memory.inl>

#include <testing/testing.h>

PAW_TEST(IsPointerAligned)
{
	for (PtrSize i = 0; i < (1ull << 16ull); i++)
	{
		Byte const* const ptr = reinterpret_cast<Byte*>(i);
		if (i % 4 == 0)
		{
			PAW_TEST_EXPECT(IsPointerAligned(ptr, 4));
		}
		else
		{
			PAW_TEST_EXPECT_NOT(IsPointerAligned(ptr, 4));
		}

		if (i % 8 == 0)
		{
			PAW_TEST_EXPECT(IsPointerAligned(ptr, 8));
		}
		else
		{
			PAW_TEST_EXPECT_NOT(IsPointerAligned(ptr, 8));
		}

		if (i % 16 == 0)
		{
			PAW_TEST_EXPECT(IsPointerAligned(ptr, 16));
		}
		else
		{
			PAW_TEST_EXPECT_NOT(IsPointerAligned(ptr, 16));
		}

		if (i % 32 == 0)
		{
			PAW_TEST_EXPECT(IsPointerAligned(ptr, 32));
		}
		else
		{
			PAW_TEST_EXPECT_NOT(IsPointerAligned(ptr, 32));
		}
	}
}