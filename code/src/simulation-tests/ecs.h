#pragma once

#include <core/reflected_type.h>

PAW_REFLECT_ENUM()
enum class EntityComponentType
{
	StaticMesh,
	DynamicMesh,
};

PAW_REFLECT_ENUM()
enum class EntitySystemType
{
	Test,
};