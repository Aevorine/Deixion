// 诊断工具：打印 WinEvent 钩子看到的界面事件（事件号、窗口、类名、进程、对象），用来核对“动作后有没有反应”的探测依据。
#include <windows.h>

#include <cstdio>
#include <cstdlib>

static void CALLBACK proc(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
  if (idObject == OBJID_CURSOR || idObject == OBJID_CARET) return;
  wchar_t cls[64] = L"";
  if (hwnd) GetClassNameW(hwnd, cls, 64);
  DWORD pid = 0;
  if (hwnd) GetWindowThreadProcessId(hwnd, &pid);
  HWND root = hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr;
  std::printf("ev=0x%04lX hwnd=%p root=%p pid=%lu obj=%ld child=%ld cls=%ls\n", ev, static_cast<void*>(hwnd), static_cast<void*>(root), pid, idObject, idChild, cls);
}

int main(int argc, char** argv) {
  const int secs = argc > 1 ? atoi(argv[1]) : 3;
  HWINEVENTHOOK h = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_VALUECHANGE, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  const DWORD end = GetTickCount() + static_cast<DWORD>(secs) * 1000;
  MSG m;
  while (GetTickCount() < end) {
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
  }
  UnhookWinEvent(h);
  return 0;
}
