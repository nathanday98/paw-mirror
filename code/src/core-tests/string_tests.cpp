#include <core/string.h>

#include <testing/testing.h>

PAW_TEST(cstrings_equal)
{
	PAW_TEST_EXPECT(CStringsEqual("aaaa", "aaaa"));
	PAW_TEST_EXPECT_NOT(CStringsEqual("aaaa", "bbbb"));
	PAW_TEST_EXPECT_NOT(CStringsEqual("aa", "a"));
	PAW_TEST_EXPECT_NOT(CStringsEqual("a", "aa"));
	PAW_TEST_EXPECT(CStringsEqual("", ""));
	PAW_TEST_EXPECT_NOT(CStringsEqual("", "a"));
	PAW_TEST_EXPECT_NOT(CStringsEqual("a", ""));
}