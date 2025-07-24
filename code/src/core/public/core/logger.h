#pragma once

#include <core/std.h>
#include <core/src_location_types.h>

namespace Logger
{
	enum class Severity : U32
	{
		Info,
		Success,
		Warning,
		Error,
	};

	void Log(Severity severity, char const* format, ...);
}

#define PAW_INFO(fmt, ...) Logger::Log(Logger::Severity::Info, fmt, __VA_ARGS__)
#define PAW_SUCCESS(fmt, ...) Logger::Log(Logger::Severity::Success, fmt, __VA_ARGS__)
#define PAW_WARNING(fmt, ...) Logger::Log(Logger::Severity::Warning, fmt, __VA_ARGS__)
#define PAW_ERROR(fmt, ...) Logger::Log(Logger::Severity::Error, fmt, __VA_ARGS__)