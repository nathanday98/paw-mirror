#include "testing_platform.h"

#include <Windows.h>

void platform_setup_console()
{
	// Enable color outputs
	HANDLE handle_out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD console_mode;
	GetConsoleMode(handle_out, &console_mode);
	console_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	console_mode |= DISABLE_NEWLINE_AUTO_RETURN;
	SetConsoleMode(handle_out, console_mode);
}

bool platform_is_debugger_present()
{
	return IsDebuggerPresent();
}
