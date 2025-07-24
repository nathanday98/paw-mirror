#pragma once

#define PAW_UNUSED_ARG(arg) (void)arg

#define PAW_ERROR_ON_PADDING_BEGIN __pragma(warning(push)) __pragma(warning(error : 4820))
#define PAW_ERROR_ON_PADDING_END __pragma(warning(pop))
#define PAW_DISABLE_ALL_WARNINGS_BEGIN __pragma(warning(push, 0))
#define PAW_DISABLE_ALL_WARNINGS_END __pragma(warning(pop))

#include <cstdint>

using S8 = std::int8_t;
using S16 = std::int16_t;
using S32 = std::int32_t;
using S64 = std::int64_t;

using U8 = std::uint8_t;
using U16 = std::uint16_t;
using U32 = std::uint32_t;
using U64 = std::uint64_t;
using PtrSize = size_t;
using PtrDiff = ptrdiff_t;
using UPtr = uintptr_t;

using Byte = U8;

using F32 = float;
using F64 = double;

constexpr S32 g_s32_max = 2147483647i32;
constexpr S32 g_s32_min = (-2147483647i32 - 1);
constexpr F32 g_f32_max = 3.402823466e+38F;

#define PAW_ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))
#define PAW_CONCAT_EX(x, y) x##y
#define PAW_CONCAT(x, y) PAW_CONCAT_EX(x, y)

class NonCopyable
{
public:
	NonCopyable() = default;

	NonCopyable(NonCopyable const&) = delete;
	NonCopyable& operator=(NonCopyable const&) = delete;

	NonCopyable(NonCopyable&&) = default;
	NonCopyable& operator=(NonCopyable&&) = default;
};

#define FORCE_INLINE __attribute__((always_inline))
// #define FORCE_INLINE

template <typename T>
struct RemoveReference_t
{
	using Type = T;
};
template <typename T>
struct RemoveReference_t<T&>
{
	using Type = T;
};
template <typename T>
struct RemoveReference_t<T&&>
{
	using Type = T;
};

#define PAW_MOVE(...) static_cast<RemoveReference_t<decltype(__VA_ARGS__)>::Type&&>(__VA_ARGS__)

template <typename Func>
struct Defer
{
	Defer(Func&& func, bool trigger = true)
		: func(func)
		, trigger(trigger)
	{
	}

	~Defer()
	{
		if (trigger)
		{
			func();
		}
	}

	Func func;
	bool trigger;
};
