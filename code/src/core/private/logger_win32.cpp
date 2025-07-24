#include <core/logger.h>

#include <cstdio>
#include <cstdarg>

static constexpr char const* g_severities[]{
	"Info",
	"Success",
	"Warning",
	"Error",
};

// #TODO: Pipe this into win32 properly
void Logger::Log(Severity severity, char const* format, ...)
{
	FILE* const stream = severity == Severity::Error ? stderr : stdout;

	va_list args;
	va_start(args, format);

	std::fprintf(stream, "[%s]: ", g_severities[static_cast<S32>(severity)]);
	std::vfprintf(stream, format, args);
	std::fprintf(stream, "\n");
	va_end(args);
}
