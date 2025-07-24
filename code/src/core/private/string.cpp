#include <core/string.h>

bool CStringsEqual(char const* a, char const* b)
{
	char const* a_walker = a;
	char const* b_walker = b;
	while (true)
	{
		if (*a_walker != *b_walker)
		{
			return false;
		}

		if (*a_walker == 0 || *b_walker == 0)
		{
			return true;
		}

		a_walker++;
		b_walker++;
	}
	return true;
}
