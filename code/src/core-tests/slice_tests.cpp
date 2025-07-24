#include <core/slice.inl>

#include <testing/testing.h>

PAW_TEST(Slice)
{
	S32 a = 0;
	Slice<S32 const> x{&a, 1};

	PAW_TEST_EXPECT_EQUAL(x.count, 1);
	PAW_TEST_EXPECT(x.items == &a);
}

PAW_TEST(from_start_end_ptrs)
{
	S32 arr[2]{};
	Slice<S32 const> x = ConstSliceFromStartEnd(arr, arr + PAW_ARRAY_COUNT(arr));
	PAW_TEST_EXPECT_EQUAL(x.count, 2);
	PAW_TEST_EXPECT_EQUAL(x.items, static_cast<S32 const*>(arr));
}

PAW_TEST(from_init_list)
{
	Slice<S32 const> x{1, 2, 3, 4, 5};
	PAW_TEST_EXPECT_EQUAL(x.count, 5);
	PAW_TEST_EXPECT_EQUAL(x[0], 1);
	PAW_TEST_EXPECT_EQUAL(x[1], 2);
	PAW_TEST_EXPECT_EQUAL(x[2], 3);
	PAW_TEST_EXPECT_EQUAL(x[3], 4);
	PAW_TEST_EXPECT_EQUAL(x[4], 5);
}

PAW_TEST(calc_size_Bytes)
{
	F64 arr[10]{};
	Slice<F64 const> x = ConstSliceFromStartEnd(arr, arr + PAW_ARRAY_COUNT(arr));
	PAW_TEST_EXPECT_EQUAL(CalcTotalSizeBytes(x), sizeof(arr));
}

PAW_TEST(ToConstSlice)
{
	S32 a = 0;
	Slice<S32> x{&a, 1};
	Slice<S32 const> y = ToConstSlice(x);
	PAW_TEST_EXPECT_EQUAL(static_cast<S32 const*>(x.items), y.items);
	PAW_TEST_EXPECT_EQUAL(x.count, y.count);
}

PAW_TEST(SubSlice)
{
	Slice<S32 const> x{1, 2, 3, 4, 5};
	Slice<S32 const> y = SubSlice(x, 1, 3);
	PAW_TEST_EXPECT_EQUAL(y.count, 3);
	PAW_TEST_EXPECT_EQUAL(y[0], 2);
	PAW_TEST_EXPECT_EQUAL(y[1], 3);
	PAW_TEST_EXPECT_EQUAL(y[2], 4);
}

PAW_TEST(ConstSubSlice)
{
	S32 arr[5]{1, 2, 3, 4, 5};
	Slice<S32> x = SliceFromStartEnd(arr, arr + PAW_ARRAY_COUNT(arr));
	Slice<S32 const> y = ConstSubSlice(x, 1, 3);
	PAW_TEST_EXPECT_EQUAL(y.count, 3);
	PAW_TEST_EXPECT_EQUAL(y[0], 2);
	PAW_TEST_EXPECT_EQUAL(y[1], 3);
	PAW_TEST_EXPECT_EQUAL(y[2], 4);
}

PAW_TEST(iterator)
{
	S32 arr[5]{1, 2, 3, 4, 5};
	Slice<S32> x = SliceFromStartEnd(arr, arr + PAW_ARRAY_COUNT(arr));
	S32 index = 0;
	for (S32& it : x)
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}

	index = 0;
	for (S32 it : x)
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}
}

PAW_TEST(const_iterator)
{
	Slice<S32 const> x{{1, 2, 3, 4, 5}};
	S32 index = 0;
	for (S32 const& it : x)
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}

	index = 0;
	for (S32 it : x)
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}
}

PAW_TEST(ReverseIterator)
{
	S32 arr[5]{5, 4, 3, 2, 1};
	Slice<S32> x = SliceFromStartEnd(arr, arr + PAW_ARRAY_COUNT(arr));
	S32 index = 0;
	for (S32 it : ReverseIterator(x))
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}

	index = 0;
	for (S32 it : ReverseIterator(x))
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}
}

PAW_TEST(reverse_const_iterator)
{
	Slice<S32 const> x{5, 4, 3, 2, 1};
	S32 index = 0;
	for (S32 const it : ReverseIterator(x))
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}

	index = 0;
	for (S32 const it : ReverseIterator(x))
	{
		PAW_TEST_EXPECT_EQUAL(it, index + 1);
		index++;
	}
}