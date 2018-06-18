#include "stdafx.h"
#include "common.h"

#include <algorithm>
#include <cctype>

// TODO(hoangpq): Write template function for bot SplitString and Trim.
// So we can access this function for both std::string and std::wstring.
namespace common {

std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim) {
  std::vector<std::wstring> ret;
  std::size_t curr = s.find(delim);
  std::size_t prev = 0;

  while (curr != std::wstring::npos) {
    ret.push_back(s.substr(prev, curr - prev));
    prev = curr + 1;
    curr = s.find(delim, prev);
  }
  ret.push_back(s.substr(prev, curr - prev));

  return ret;
}

std::wstring Trim(const std::wstring &s) {
  auto const is_space = [](int c) {
    return std::isspace(c);
  };

  auto wsfront = std::find_if_not(s.begin(), s.end(), is_space);
  auto wsback = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
  return (wsback <= wsfront ? std::wstring() : std::wstring(wsfront, wsback));
}

}
