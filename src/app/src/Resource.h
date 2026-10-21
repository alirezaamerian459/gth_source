#pragma once

// Macro-only (no enum/C++ syntax) so this can be #include'd from both
// AppResources.rc (rc.exe only understands preprocessor directives, not
// real C++ declarations - see ControlIds.h's enum, which is why that
// header can't be shared with the .rc file) and any .cpp that needs to
// reference the icon resource by ID (currently none do - Windows picks the
// first RT_ICON group in the .exe automatically for the taskbar/Explorer/
// Alt-Tab icon - but the symbol is here if that ever changes).
#define IDI_APP_ICON 101
