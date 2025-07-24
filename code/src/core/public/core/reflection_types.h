#pragma once

#include <core/std.h>

class ClassInfo;

class ReflectedClass : NonCopyable
{
public:
	virtual ~ReflectedClass()
	{
	}
	virtual ClassInfo const& GetTypeInfo() const = 0;
};