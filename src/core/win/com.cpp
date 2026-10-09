#include "core/win/com.hpp"

#include <cstdio>

#include "core/base/text.hpp"

namespace dx {

void com_init_thread() {
  struct Guard {
    bool ok{false};
    Guard() {
      const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      ok = SUCCEEDED(hr);
    }
    ~Guard() {
      if (ok) CoUninitialize();
    }
  };
  thread_local Guard g;
  (void)g;
}

std::string bstr_to_utf8(BSTR b) { return b ? text::narrow(std::wstring_view(b, SysStringLen(b))) : std::string(); }

std::string hr_text(HRESULT hr) {
  char b[24];
  std::snprintf(b, sizeof b, "0x%08lX", static_cast<unsigned long>(hr));
  return b;
}

}  // namespace dx
