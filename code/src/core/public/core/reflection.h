#pragma once

#include <core/reflection_types.h>

class ClassInfo : NonCopyable
{
public:
	ClassInfo(ClassInfo const* parent)
		: parent(parent)
	{
	}

	bool IsDerivedFrom(ClassInfo const& type) const;
	bool IsType(ClassInfo const& type) const;

private:
	ClassInfo const* parent;
};