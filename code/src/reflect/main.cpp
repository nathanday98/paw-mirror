// PAW_DISABLE_ALL_WARNINGS_BEGIN
#include <stdio.h>
#include <assert.h>

#include <filesystem>
#include <vector>

// PAW_DISABLE_ALL_WARNINGS_END

static bool is_letter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_number(char c)
{
	return (c >= '0' && c <= '9');
}

enum TokenType
{
	TokenType_Unknown,
	TokenType_Identifier,
	TokenType_OpenParen,
	TokenType_CloseParen,
	TokenType_OpenBracket,
	TokenType_CloseBracket,
	TokenType_OpenCurlyBrace,
	TokenType_CloseCurlyBrace,
	TokenType_Integer,
	TokenType_Float,
	TokenType_Hash,
	TokenType_Plus,
	TokenType_Minus,
	TokenType_Backslash,
	TokenType_Forwardslash,
	TokenType_Equal,
	TokenType_Comma,
	TokenType_Semicolon,
	TokenType_Colon,
	TokenType_Asterisk,

	TokenType_Enum,
	TokenType_Class,
	TokenType_Define,

	TokenType_ReflectEnum,

	TokenType_EndOfFile,
	TokenType_Count,
};

static constexpr char const* token_names[TokenType_Count] = {
	"Unknown",
	"Identifier",
	"OpenParen",
	"CloseParen",
	"OpenBracket",
	"CloseBracket",
	"OpenCurlyBrace",
	"CloseCurlyBrace",
	"Integer",
	"Float",
	"Hash",
	"Plus",
	"Minus",
	"Backslash",
	"Forwardslash",
	"Equal",
	"Comma",
	"Semicolon",
	"Colon",
	"Asterisk",

	"enum",
	"class",
	"define",

	"PAW_REFLECT_ENUM",

	"EndOfFile",
};

struct Token_t
{
	char const* start = nullptr;
	int length = 0;
	TokenType type = TokenType_Unknown;
	union
	{
		int integer;
	} value;
};

static void skip_token(Token_t const*& token_ptr, TokenType expected)
{
	if (token_ptr->type != expected)
	{
		fprintf(stderr, "Expected token %s, but received %s\n", token_names[expected], token_names[token_ptr->type]);
	}
	// assert(token_ptr->type == expected);
	token_ptr++;
}

static void skip_to_after_token(Token_t const*& token_ptr, TokenType skip_to)
{
	while (token_ptr->type != skip_to)
	{
		token_ptr++;
	}
	token_ptr++;
}

static void skip_to_token(Token_t const*& token_ptr, TokenType skip_to)
{
	while (token_ptr->type != skip_to)
	{
		token_ptr++;
	}
}

void parse_number(char& c, Token_t& token, char const*& buffer_ptr)
{
	int number = c - '0';
	token.type = TokenType_Integer;
	while (true)
	{
		c = buffer_ptr[token.length];
		if (is_number(c))
		{
			number *= 10;
			number += c - '0';
			token.length++;
		}
		else if (c == '_')
		{
			token.length++;
			continue;
		}
		else
		{
			break;
		}
	}

	token.length++;
	token.value.integer = number;

	buffer_ptr += token.length - 1;
}

int main(int arg_count, char const* args[])
{

	if (arg_count < 3)
	{
		fprintf(stderr, "Arguments are not in correct format: [type info header] [type info cpp] [input + output pairs]");
		return -1;
	}

	for (auto const& path : std::filesystem::directory_iterator("."))
	{
		std::filesystem::remove_all(path);
	}

	char const* const type_info_header = args[1];
	char const* const type_info_cpp = args[2];

	(void)type_info_cpp;
	(void)type_info_header;

	for (int arg_index = 3; arg_index < arg_count; arg_index++)
	{
		char const* const input = args[arg_index];

		FILE* input_file = fopen(input, "rb");
		fseek(input_file, 0, SEEK_END);
		long file_size_Bytes = ftell(input_file);
		fseek(input_file, 0, SEEK_SET);
		char* file_contents = (char*)malloc(file_size_Bytes + 1);
		file_contents[file_size_Bytes] = 0;
		char const* file_end = file_contents + file_size_Bytes;
		fread(file_contents, file_size_Bytes, 1, input_file);
		fclose(input_file);

		std::vector<Token_t> tokens;

		char const* buffer_ptr = file_contents;
		while (buffer_ptr != file_end)
		{
			char c = *buffer_ptr++;
			while ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && c != 0)
			{
				c = *buffer_ptr++;
				continue;
			}

			if (c == 0)
			{
				break;
			}

			if (c == '/')
			{
				if (*buffer_ptr == '*')
				{
					char last_char = *buffer_ptr;
					while (buffer_ptr++ != file_end)
					{
						if (*buffer_ptr == '/' && last_char == '*')
						{
							break;
						}
						last_char = *buffer_ptr;
					}
					buffer_ptr++;
					continue;
				}
				else if (*buffer_ptr == '/')
				{
					while (*buffer_ptr++ != '\n')
					{
					}
					continue;
				}
			}

			char const* const start = buffer_ptr - 1;

			Token_t token{};
			token.start = start;

			switch (c)
			{
				case '(':
				{
					token.type = TokenType_OpenParen;
					token.length = 1;
				}
				break;
				case ')':
				{
					token.type = TokenType_CloseParen;
					token.length = 1;
				}
				break;
				case '[':
				{
					token.type = TokenType_OpenBracket;
					token.length = 1;
				}
				break;
				case ']':
				{
					token.type = TokenType_CloseBracket;
					token.length = 1;
				}
				break;
				case '{':
				{
					token.type = TokenType_OpenCurlyBrace;
					token.length = 1;
				}
				break;
				case '}':
				{
					token.type = TokenType_CloseCurlyBrace;
					token.length = 1;
				}
				break;
				case '#':
				{
					token.type = TokenType_Hash;
					token.length = 1;
				}
				break;
				case '+':
				{
					token.type = TokenType_Plus;
					token.length = 1;
				}
				break;
				case '-':
				{
					token.type = TokenType_Minus;
					token.length = 1;
					if (is_number(*buffer_ptr))
					{
						char new_c = *buffer_ptr;
						parse_number(new_c, token, buffer_ptr);
						token.value.integer *= -1;
					}
				}
				break;
				case '\\':
				{
					token.type = TokenType_Backslash;
					token.length = 1;
				}
				break;
				case '/':
				{
					token.type = TokenType_Forwardslash;
					token.length = 1;
				}
				break;
				case '=':
				{
					token.type = TokenType_Equal;
					token.length = 1;
				}
				break;

				case ',':
				{
					token.type = TokenType_Comma;
					token.length = 1;
				}
				break;

				case ';':
				{
					token.type = TokenType_Semicolon;
					token.length = 1;
				}
				break;

				case ':':
				{
					token.type = TokenType_Colon;
					token.length = 1;
				}
				break;

				case '*':
				{
					token.type = TokenType_Asterisk;
					token.length = 1;
				}
				break;

				default:
				{
					if (is_letter(c) || c == '_')
					{
						token.type = TokenType_Identifier;
						c = buffer_ptr[token.length];
						while (is_letter(c) || is_number(c) || c == '_')
						{
							token.length++;
							c = buffer_ptr[token.length];
						}

						token.length++;

						buffer_ptr += token.length - 1;

						std::string_view const keyword{token.start, (size_t)token.length};

						if (keyword == std::string_view("enum"))
						{
							token.type = TokenType_Enum;
						}
						else if (keyword == std::string_view("class"))
						{
							token.type = TokenType_Class;
						}
						else if (keyword == std::string_view("define"))
						{
							token.type = TokenType_Define;
						}
						else if (keyword == std::string_view("PAW_REFLECT_ENUM"))
						{
							token.type = TokenType_ReflectEnum;
						}
					}
					else if (is_number(c))
					{
						parse_number(c, token, buffer_ptr);
					}
					else
					{
						token.type = TokenType_Unknown;
						token.length = 1;
					}
				}
			}

			switch (token.type)
			{
				case TokenType_Identifier:
				{
					// printf("Token: %s - %.*s\n", token_names[token.type], token.length, token.start);
				}
				break;

				default:
				{
					// printf("Token: %s - %.*s\n", token_names[token.type], token.length, token.start);
				}
				break;
			}

			tokens.push_back(token);
		}

		{
			Token_t end_token{};
			end_token.type = TokenType::TokenType_EndOfFile;
			tokens.push_back(end_token);
		}

		std::filesystem::path input_path{input};
		std::filesystem::path output_cpp_filename = input_path.filename();
		output_cpp_filename.replace_extension(".generated.cpp");

		std::filesystem::path output_h_filename = input_path.filename();
		output_h_filename.replace_extension(".generated.h");

		FILE* output_h_file = fopen(output_h_filename.string().c_str(), "wb");
		fprintf(output_h_file, "#pragma once\n");
		fprintf(output_h_file, "#include <core/std.h>\n");

		FILE* output_cpp_file = fopen(output_cpp_filename.string().c_str(), "wb");
		fprintf(output_cpp_file, "#include <%s>\n", input);
		fprintf(output_cpp_file, "#include <core/reflection.h>\n");

		struct EnumField_t
		{
			std::string_view name;
			int value;
		};

		std::vector<EnumField_t> enum_fields;

		Token_t const* token = &tokens[0];
		while (token->type != TokenType_EndOfFile)
		{
			switch (token->type)
			{
				case TokenType_ReflectEnum:
				{
					token++;
					skip_token(token, TokenType_OpenParen);
					skip_token(token, TokenType_CloseParen);
					if (token->type == TokenType_Enum)
					{
						skip_token(token, TokenType_Enum);
						skip_token(token, TokenType_Class);
						// printf("Reflecting enum: %.*s\n", token->length, token->start);
						std::string_view enum_name{token->start, size_t(token->length)};
						skip_token(token, TokenType_Identifier);
						skip_token(token, TokenType_OpenCurlyBrace);
						int enum_value = 0;
						enum_fields.clear();
						while (token->type == TokenType_Identifier)
						{
							Token_t const& field_token = *token;
							token++;
							if (token->type == TokenType_Equal)
							{
								token++;
								if (token->type == TokenType_Integer)
								{
									enum_value = token->value.integer;
								}
							}
							while (token->type != TokenType_Comma && token->type != TokenType_CloseCurlyBrace)
							{
								token++;
							}
							token++;

							// printf("\tField: %.*s - %d\n", field_token.length, field_token.start, enum_value);
							enum_fields.push_back({std::string_view(field_token.start, size_t(field_token.length)), enum_value});
							enum_value++;
						}

						if (token->type == TokenType_CloseCurlyBrace)
						{
							token++;
						}
						skip_token(token, TokenType_Semicolon);

						fprintf(output_cpp_file, R"(
char const* get_enum_value_name(%.*s value)
{
	switch(value)
	{)",
								int(enum_name.size()),
								enum_name.data());

						for (EnumField_t const& field : enum_fields)
						{
							fprintf(output_cpp_file, R"(
		case %.*s::%.*s: { return "%.*s"; } break;)",
									int(enum_name.size()),
									enum_name.data(),
									int(field.name.size()),
									field.name.data(),
									int(field.name.size()),
									field.name.data());
						}

						fprintf(output_cpp_file, R"(
	}
	return "This is an error and should not happen!!";
}
)");

						fprintf(output_h_file, R"(

static constexpr S32 %.*s_value_count = %d;
char const* get_enum_value_name(%.*s value);
)",
								int(enum_name.size()),
								enum_name.data(),
								int(enum_fields.size()),
								int(enum_name.size()),
								enum_name.data());
					}
				}
				break;

				default:
				{
					token++;
				}
				break;
			}
		}

		fclose(output_cpp_file);
		fclose(output_h_file);
	}

	{
		FILE* file = fopen(type_info_cpp, "wb");
		fprintf(file, "// type info cpp\n");
		fclose(file);
	}

	{
		FILE* file = fopen(type_info_header, "wb");
		fprintf(file, "// type info h\n");
		fclose(file);
	}
}