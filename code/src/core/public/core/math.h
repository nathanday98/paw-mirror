#include <core/math_types.h>
#include <core/assert.h>

inline Float2 operator-(Float2 rhs)
{
	return {-rhs.x, -rhs.y};
}

inline Float2 operator+(Float2 lhs, Float2 rhs)
{
	return {lhs.x + rhs.x, lhs.y + rhs.y};
}

inline Float2 operator-(Float2 lhs, Float2 rhs)
{
	return {lhs.x - rhs.x, lhs.y - rhs.y};
}

inline Float2 operator*(Float2 vec, F32 scalar)
{
	return {vec.x * scalar, vec.y * scalar};
}

inline Float2 operator*(F32 scalar, Float2 vec)
{
	return {vec.x * scalar, vec.y * scalar};
}

inline Float2 operator*(Float2 lhs, Float2 rhs)
{
	return {lhs.x * rhs.x, lhs.y * rhs.y};
}

inline Float2 operator/(Float2 vec, F32 scalar)
{
	return {vec.x / scalar, vec.y / scalar};
}

inline Float2 operator/(F32 scalar, Float2 vec)
{
	return {vec.x / scalar, vec.y / scalar};
}

inline Float2 operator/(Float2 lhs, Float2 rhs)
{
	return {lhs.x / rhs.x, lhs.y / rhs.y};
}

inline void operator+=(Float2& lhs, Float2 rhs)
{
	lhs = {lhs.x + rhs.x, lhs.y + rhs.y};
}

inline void operator-=(Float2& lhs, Float2 rhs)
{
	lhs = {lhs.x - rhs.x, lhs.y - rhs.y};
}

inline void operator*=(Float2& lhs, F32 rhs)
{
	lhs = {lhs.x * rhs, lhs.y * rhs};
}

inline void operator*=(Float2& lhs, Float2 rhs)
{
	lhs = {lhs.x * rhs.x, lhs.y * rhs.y};
}

inline void operator/=(Float2& lhs, F32 rhs)
{
	lhs = {lhs.x / rhs, lhs.y / rhs};
}

inline void operator/=(Float2& lhs, Float2 rhs)
{
	lhs = {lhs.x / rhs.x, lhs.y / rhs.y};
}

inline F32 LengthSquared(Float2 vec)
{
	return (vec.x * vec.x) + (vec.y * vec.y);
}

inline F32 Float2::operator[](S32 index) const
{
	PAW_ASSERT(index >= 0 && index <= 2, "Index is not in range of 2");
	return reinterpret_cast<F32 const*>(this)[index];
}

inline F32& Float2::operator[](S32 index)
{
	PAW_ASSERT(index >= 0 && index <= 2, "Index is not in range of 2");
	return reinterpret_cast<F32*>(this)[index];
}

inline F32 Dot(Float2 lhs, Float2 rhs)
{
	return (lhs.x * rhs.x) + (lhs.y * rhs.y);
}

inline F32 SquareRoot(F32 x)
{
	return __builtin_sqrtf(x);
}

inline F32 Length(Float2 x)
{
	return SquareRoot((x.x * x.x) + (x.y * x.y));
}

inline Float2 Normalize(Float2 x)
{
	return x / Length(x);
}

inline F32 Max(F32 a, F32 b)
{
	return a > b ? a : b;
}

inline U32 Max(U32 a, U32 b)
{
	return a > b ? a : b;
}

inline U64 Max(U64 a, U64 b)
{
	return a > b ? a : b;
}

inline F32 Min(F32 a, F32 b)
{
	return a < b ? a : b;
}

inline F32 Clamp(F32 x, F32 min_value, F32 max_value)
{
	return x < min_value ? min_value : (x > max_value ? max_value : x);
}

inline F32 Floor(F32 x)
{
	return __builtin_floorf(x);
}

inline F32 Ceil(F32 x)
{
	return __builtin_ceilf(x);
}

inline F32 Round(F32 x)
{
	return __builtin_roundf(x);
}

inline F32 Float3::operator[](S32 index) const
{
	PAW_ASSERT(index >= 0 && index <= 3, "Index is not in range of 3");
	return reinterpret_cast<F32 const*>(this)[index];
}

inline F32& Float3::operator[](S32 index)
{
	PAW_ASSERT(index >= 0 && index <= 3, "Index is not in range of 3");
	return reinterpret_cast<F32*>(this)[index];
}

inline F32 Float4::operator[](S32 index) const
{
	PAW_ASSERT(index >= 0 && index <= 4, "Index is not in range of 4");
	return reinterpret_cast<F32 const*>(this)[index];
}

inline F32& Float4::operator[](S32 index)
{
	PAW_ASSERT(index >= 0 && index <= 4, "Index is not in range of 4");
	return reinterpret_cast<F32*>(this)[index];
}

inline constexpr S32 BitScanMSB(U64 x)
{
	return 63 - __builtin_clzll(x);
}

inline constexpr S32 BitScanLSB(U64 x)
{
	return __builtin_ctzll(x);
}