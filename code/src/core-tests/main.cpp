
#include <core/std.h>
#include <core/arena.h>
#include <core/memory.inl>
#include <core/string.h>

#include <testing/testing.h>

#define PAW_TEST_MODULE_NAME Thing

inline int int_drop_sign(int x)
{
	U32 const x_u = static_cast<U32>(x);
	U32 y = 1 << 31;
	return int((x_u ^ y) - 1u);
}

constexpr int string_length(char const* const str)
{
	int length = 0;
	for (; str[length] != 0; length++)
	{
	}
	return length;
}

constexpr int get_next_format_index(int start_index, char const* const str, int str_length)
{
	int length = start_index;
	for (; str[length] != '{' && length < str_length; length++)
	{
	}
	return length;
}

enum class Formatter
{
	None,
};

template <typename T>
consteval char get_type_char();

template <>
consteval char get_type_char<int>()
{
	return 'i';
}

template <>
consteval char get_type_char<double>()
{
	return 'd';
}

constexpr void format_arg(char*& buffer_ptr, int x, Formatter /*formatter*/)
{
	U32 digit = 0;
	if (x < 0)
	{
		*buffer_ptr++ = '-';
		digit = (~U32(x)) + 1;
	}
	else
	{
		digit = U32(x);
	}
	char* const number_buffer = buffer_ptr;
	// int digit = int_drop_sign(x);
	int digit_count = 0;
	while (digit > 0)
	{
		char const c = (digit % 10) + '0';
		*buffer_ptr++ = c;
		digit /= 10;
		digit_count++;
	}

	for (int i = 0; i < digit_count / 2; i++)
	{
		int const swap_index = digit_count - i - 1;
		char temp = number_buffer[i];
		number_buffer[i] = number_buffer[swap_index];
		number_buffer[swap_index] = temp;
	}
}

template <typename... Args>
constexpr int parse_format(char* const buffer, char const* const format, Args&&... args)
{
	int format_length = string_length(format);
	int format_char_index = -1;
	int result = 0;
	char* buffer_ptr = buffer;
	while ((format_char_index = get_next_format_index(format_char_index + 1, format, format_length)) < format_length)
	{
		char c = format[format_char_index + 1];
		(void)c;
		Formatter formatter = Formatter::None;

		int arg_index = 0;
		([&]
		 {
			 if  (arg_index == result)
			 {
				 format_arg(buffer_ptr, args, formatter);
			 }

			 arg_index++; }(),
		 ...);

		result++;
	}
	return result;
}

template <typename Format, typename... Args>
int format(Format format, Args&&... /*args*/)
{

	return parse_format(format);
}

PAW_TEST(format_arg)
{
	{
		char buffer[32]{};
		char* buffer_ptr = buffer;
		format_arg(buffer_ptr, 1234567, Formatter::None);
		PAW_TEST_EXPECT(CStringsEqual(buffer, "1234567"));
	}
	{
		char buffer[32]{};
		char* buffer_ptr = buffer;
		format_arg(buffer_ptr, 12345678, Formatter::None);
		PAW_TEST_EXPECT(CStringsEqual(buffer, "12345678"));
	}
	{
		char buffer[32]{};
		char* buffer_ptr = buffer;
		format_arg(buffer_ptr, -1234, Formatter::None);
		PAW_TEST_EXPECT(CStringsEqual(buffer, "-1234"));
	}
	{
		char buffer[32]{};
		char* buffer_ptr = buffer;
		format_arg(buffer_ptr, g_s32_max, Formatter::None);
		PAW_TEST_EXPECT(CStringsEqual(buffer, "2147483647"));
	}
	{
		char buffer[32]{};
		char* buffer_ptr = buffer;
		format_arg(buffer_ptr, g_s32_min, Formatter::None);
		PAW_TEST_EXPECT(CStringsEqual(buffer, "-2147483648"));
	}
}

PAW_TEST(Hey)
{

	char buffer[32]{};
	// format("{} --- {}", int(10), double(4.0));
	int len = parse_format(buffer, "{} ---- {}", int(10), int(4.0));
	PAW_TEST_EXPECT(len == 2);
}

int main(int arg_count, char* args[])
{
	int result = test_main(arg_count, args);
	return result;
}