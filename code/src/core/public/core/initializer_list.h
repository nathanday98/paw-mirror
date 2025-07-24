#pragma once

#ifdef PAW_USE_STD_INITIALIZER_LIST

#include <initializer_list>

#else

namespace std
{
	template <typename T>
	class initializer_list
	{
	public:
		using value_type = T;
		using reference = T const&;
		using const_reference = T const&;
		using size_type = size_t;

		using iterator = T const*;
		using const_iterator = T const*;

		constexpr initializer_list() noexcept = default;

		constexpr initializer_list(T const* in_start, T const* in_end) noexcept
		{
			start_ptr = in_start;
			end_ptr = in_end;
		}

		constexpr T const* begin() const noexcept
		{
			return start_ptr;
		}
		constexpr T const* end() const noexcept
		{
			return end_ptr;
		}
		constexpr size_t size() const noexcept
		{
			return end_ptr - start_ptr;
		}

	private:
		T const* start_ptr = nullptr;
		T const* end_ptr = nullptr;
	};

	template <typename T>
	constexpr T const* begin(initializer_list<T> inInitializerList) noexcept
	{
		return inInitializerList.begin();
	}

	template <typename T>
	constexpr T const* end(initializer_list<T> inInitializerList) noexcept
	{
		return inInitializerList.end();
	}
}

#endif