#include <core/reflection.h>

bool ClassInfo::IsDerivedFrom(ClassInfo const& type) const
{
	if (&type == this)
	{
		return true;
	}

	if (parent)
	{
		return &type == parent || parent->IsDerivedFrom(type);
	}

	return false;
}

bool ClassInfo::IsType(ClassInfo const& type) const
{
	return &type == this;
}
