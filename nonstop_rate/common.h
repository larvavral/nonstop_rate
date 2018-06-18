#pragma once

#include <vector>

#define TIMEOUT_PARAM_NAME L"01.Timeout(seconds)"
#define FEEDER_PARAM_NAME L"02.Feeder"
#define SYMBOLS_PARAM_NAME L"03.Symbols"

namespace common {

// Split string.
std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim = L' ');

// Trim from both start and end of string.
std::wstring Trim(const std::wstring &s);

}
