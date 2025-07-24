#pragma once

#include <core/memory_types.h>

#include "fonts_types.h"

namespace Fonts
{
	State& Init(IAllocator* allocator);
	void Deinit(State& state);
}