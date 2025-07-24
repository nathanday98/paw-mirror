#pragma once

#include <core/std.h>
#include <core/reflected_type.h>

PAW_REFLECT_ENUM()
enum class TestEnum0
{
	TestField1,
	TestField2
};

PAW_REFLECT_ENUM()
enum class TestEnum1
{
	TestField3,
	TestField4,
};

PAW_REFLECT_ENUM()
enum class TestEnum2
{
	TestField5 = 0,
	TestField6
};

PAW_REFLECT_ENUM()
enum class TestEnum3
{
	TestField7,
	TestField8 = 1
};

PAW_REFLECT_ENUM()
enum class TestEnum4
{
};