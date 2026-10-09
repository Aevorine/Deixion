// 仅用于端到端验证的目标窗口：按钮、勾选框、单行/多行文本框；每次变化把状态写进 state 文件，供脚本核对。
#include <windows.h>

#include <cstdio>
#include <string>

namespace {
HWND g_edit, g_btn, g_check, g_multi, g_label;
int g_count = 0;
int g_lx = -1, g_ly = -1;
std::wstring g_state_path;

std::string utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}
std::string esc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c < 0x20) o += ' ';
    else o += static_cast<char>(c);
  }
  return o;
}
std::wstring text_of(HWND h) {
  const int n = GetWindowTextLengthW(h);
  std::wstring w(static_cast<size_t>(n) + 1, L'\0');
  GetWindowTextW(h, w.data(), n + 1);
  w.resize(static_cast<size_t>(n));
  return w;
}
void save() {
  char buf[4096];
  std::snprintf(buf, sizeof buf, "{\"count\":%d,\"edit\":\"%s\",\"multi\":\"%s\",\"check\":%d,\"lx\":%d,\"ly\":%d}", g_count, esc(utf8(text_of(g_edit))).c_str(),
                esc(utf8(text_of(g_multi))).c_str(), static_cast<int>(SendMessageW(g_check, BM_GETCHECK, 0, 0)), g_lx, g_ly);
  const std::wstring tmp = g_state_path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD w = 0;
  WriteFile(h, buf, static_cast<DWORD>(std::strlen(buf)), &w, nullptr);
  CloseHandle(h);
  MoveFileExW(tmp.c_str(), g_state_path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(nullptr);
      g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 20, 20, 360, 28, h, reinterpret_cast<HMENU>(101), hi, nullptr);
      g_btn = CreateWindowExW(0, L"BUTTON", L"Press Me", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 66, 140, 36, h, reinterpret_cast<HMENU>(102), hi, nullptr);
      g_label = CreateWindowExW(0, L"STATIC", L"clicked: 0", WS_CHILD | WS_VISIBLE, 180, 74, 200, 24, h, reinterpret_cast<HMENU>(103), hi, nullptr);
      g_check = CreateWindowExW(0, L"BUTTON", L"Option", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 118, 140, 28, h, reinterpret_cast<HMENU>(104), hi, nullptr);
      g_multi = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, 20, 160, 360, 120, h, reinterpret_cast<HMENU>(105), hi, nullptr);
      HFONT f = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
      for (HWND c : {g_edit, g_btn, g_label, g_check, g_multi}) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(w), code = HIWORD(w);
      if (id == 102 && code == BN_CLICKED) {
        ++g_count;
        const std::wstring t = L"clicked: " + std::to_wstring(g_count);
        SetWindowTextW(g_label, t.c_str());
        save();
      } else if ((id == 101 || id == 105) && code == EN_CHANGE) {
        save();
      } else if (id == 104 && code == BN_CLICKED) {
        save();
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
      g_lx = LOWORD(l);
      g_ly = HIWORD(l);
      save();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  g_state_path = cmd && *cmd ? cmd : L"dx-testapp-state.json";
  while (!g_state_path.empty() && (g_state_path.front() == L'"' || g_state_path.front() == L' ')) g_state_path.erase(g_state_path.begin());
  while (!g_state_path.empty() && (g_state_path.back() == L'"' || g_state_path.back() == L' ')) g_state_path.pop_back();
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = hi;
  wc.lpszClassName = L"DxTestTarget";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, wc.lpszClassName, L"DX Test Target", WS_OVERLAPPEDWINDOW, 300, 200, 420, 340, nullptr, nullptr, hi, nullptr);
  ShowWindow(h, SW_SHOWNOACTIVATE);
  save();
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return 0;
}
