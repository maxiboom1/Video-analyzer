#pragma once

#include <windows.h>
#include <string_view>

bool StartupAuth_IsPasswordValid(std::wstring_view password);

// Returns true only after authentication. Cancel, Close, and dialog errors deny entry.
bool StartupAuth_ShowDialog(HINSTANCE instance);
