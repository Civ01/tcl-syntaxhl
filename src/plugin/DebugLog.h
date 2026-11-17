// Diagnostic logging, enabled with [General] debug_log=true.
//
// Writes tcl-syntaxhl.log next to the DLL (or to %TEMP% if that folder is not
// writable). Besides plugin events it intercepts MessageBeep / Beep /
// PlaySound in every module loaded in the process, so the log shows exactly
// which module (RichEdit, Total Commander, ...) produced a beep, with a short
// call stack. Intended for troubleshooting only.
#pragma once

#include <windows.h>

namespace shl::dbg {

// Enables or disables logging (called on every file load). Hooks are installed
// once, the first time logging is enabled, and stay for the process lifetime
// (the DLL pins itself then).
void configure(bool enabled);
bool on();

void log(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

const char* messageName(UINT msg);  // "WM_KEYDOWN", or nullptr if not interesting

}  // namespace shl::dbg
