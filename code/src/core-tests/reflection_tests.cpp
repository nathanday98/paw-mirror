#include <core/std.h>
#include <core/reflection.h>
#include <core/assert.h>
#include <core/string.h>

#include <testing/testing.h>

#include "reflection_tests.h"
// #include "reflection_tests.generated.h"
//
// #define PAW_TEST_MODULE_NAME ReflectEnum
//
// enum class Thing : U8
//{
//	A,
//	B,
// };
//
// PAW_TEST(get_enum_value_name)
//{
//	char const* value0 = get_enum_value_name(TestEnum0::TestField1);
//	PAW_TEST_EXPECT(CStringsEqual(value0, "TestField1"));
// }
//
// PAW_TEST(value_count)
//{
//	PAW_TEST_EXPECT_EQUAL(TestEnum0_value_count, 2);
//	PAW_TEST_EXPECT_EQUAL(TestEnum4_value_count, 0);
// }
