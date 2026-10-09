#include "app/wv.hpp"

#include <shlwapi.h>

#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"

namespace dx::app {
namespace {

// 这个工具链的 __uuidof 对 WebView2 接口会生成无定义的符号，所以用编译期 GUID 表代替。
constexpr int hexv(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }
constexpr unsigned long long hexn(const char* s, int n) {
  unsigned long long v = 0;
  for (int i = 0; i < n; ++i) v = v << 4 | static_cast<unsigned long long>(hexv(s[i]));
  return v;
}
constexpr GUID guid(const char (&s)[37]) {
  return GUID{static_cast<unsigned long>(hexn(s, 8)), static_cast<unsigned short>(hexn(s + 9, 4)), static_cast<unsigned short>(hexn(s + 14, 4)),
              {static_cast<unsigned char>(hexn(s + 19, 2)), static_cast<unsigned char>(hexn(s + 21, 2)), static_cast<unsigned char>(hexn(s + 24, 2)),
               static_cast<unsigned char>(hexn(s + 26, 2)), static_cast<unsigned char>(hexn(s + 28, 2)), static_cast<unsigned char>(hexn(s + 30, 2)),
               static_cast<unsigned char>(hexn(s + 32, 2)), static_cast<unsigned char>(hexn(s + 34, 2))}};
}
template <class T>
struct IidOf;
#define DX_IID(T, str)                        template <>                                 struct IidOf<T> {                             static const GUID& get() {                    static constexpr GUID g = guid(str);        return g;                                 }                                         };
DX_IID(ICoreWebView2Controller2, "c979903e-d4ca-4228-92eb-47ee3fa96eab")
DX_IID(ICoreWebView2Settings3, "fdb5ab74-af33-4854-84f0-0a631deb5eba")
DX_IID(ICoreWebView2Settings5, "183e7052-1d03-43a0-ab99-98e043b66b39")
DX_IID(ICoreWebView2Settings6, "11cb3acd-9bc8-43b8-83bf-f40753714f87")
DX_IID(ICoreWebView2WebMessageReceivedEventHandler, "57213f19-00e6-49fa-8e07-898ea01ecbd2")
DX_IID(ICoreWebView2WebResourceRequestedEventHandler, "ab00b74c-15f1-4646-80e8-e76341d25d71")
DX_IID(ICoreWebView2NavigationCompletedEventHandler, "d33a35bf-1c49-4f98-93ab-006e0533fe1c")
DX_IID(ICoreWebView2NewWindowRequestedEventHandler, "d4c185fe-c81c-4989-97af-2d3fa7ab5651")
DX_IID(ICoreWebView2PermissionRequestedEventHandler, "15e1c6a3-c72a-4df3-91d7-d097fbec6bfd")
DX_IID(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, "6c4819f3-c9b7-4260-8127-c9f5bde7f68c")
DX_IID(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, "4e8a3389-c9d8-4bd2-b6b5-124fee6cc14d")
#undef DX_IID

using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

// 把 lambda 包成 WebView2 要求的 COM 回调对象。
template <class I, class... A>
class Callback final : public I {
 public:
  explicit Callback(std::function<HRESULT(A...)> fn) : fn_(std::move(fn)) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IidOf<I>::get())) {
      *ppv = static_cast<I*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++rc_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG r = --rc_;
    if (!r) delete this;
    return r;
  }
  HRESULT STDMETHODCALLTYPE Invoke(A... a) override { return fn_(a...); }

 private:
  std::atomic<ULONG> rc_{1};
  std::function<HRESULT(A...)> fn_;
};

template <class I, class... A, class F>
I* make_cb(F&& fn) {
  return new Callback<I, A...>(std::function<HRESULT(A...)>(std::forward<F>(fn)));
}

std::string utf8_from_pwstr(PWSTR w) {
  if (!w) return {};
  std::string s = text::narrow(w);
  CoTaskMemFree(w);
  return s;
}

std::wstring dll_path() { return (paths::exe_dir() / L"WebView2Loader.dll").wstring(); }
}  // namespace

bool WebView::runtime_installed(std::string* version) {
  wchar_t buf[128] = {};
  DWORD n = sizeof buf;
  for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
    for (const wchar_t* path : {L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}", L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"}) {
      n = sizeof buf;
      if (RegGetValueW(root, path, L"pv", RRF_RT_REG_SZ, nullptr, buf, &n) == ERROR_SUCCESS && buf[0] && std::wstring(buf) != L"0.0.0.0") {
        if (version) *version = text::narrow(buf);
        return true;
      }
    }
  }
  return false;
}

WebView::~WebView() { close(); }

void WebView::close() {
  if (view_) {
    if (tok_msg_.value) view_->remove_WebMessageReceived(tok_msg_);
    if (tok_res_.value) view_->remove_WebResourceRequested(tok_res_);
    if (tok_nav_.value) view_->remove_NavigationCompleted(tok_nav_);
    if (tok_win_.value) view_->remove_NewWindowRequested(tok_win_);
    if (tok_perm_.value) view_->remove_PermissionRequested(tok_perm_);
  }
  if (ctl_) ctl_->Close();
  view_.reset();
  ctl_.reset();
  env_.reset();
}

void WebView::create(HWND host, const std::wstring& user_dir, std::function<void(bool, std::string)> ready) {
  host_ = host;
  ready_ = std::move(ready);
  HMODULE dll = LoadLibraryExW(dll_path().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  auto create_env = dll ? reinterpret_cast<CreateEnvFn>(reinterpret_cast<void*>(GetProcAddress(dll, "CreateCoreWebView2EnvironmentWithOptions"))) : nullptr;
  if (!create_env) {
    ready_(false, "WebView2Loader.dll is missing next to Deixion.exe");
    return;
  }
  auto on_env = [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
    if (FAILED(hr) || !env) {
      ready_(false, "WebView2 runtime failed to start " + hr_text(hr));
      return S_OK;
    }
    env_ = ComPtr<ICoreWebView2Environment>(env);
    env->AddRef();
    auto on_ctl = [this](HRESULT hr2, ICoreWebView2Controller* ctl) -> HRESULT {
      if (FAILED(hr2) || !ctl) {
        ready_(false, "WebView2 controller failed " + hr_text(hr2));
        return S_OK;
      }
      ctl_ = ComPtr<ICoreWebView2Controller>(ctl);
      ctl->AddRef();
      ctl_->get_CoreWebView2(view_.put());
      RECT rc;
      GetClientRect(host_, &rc);
      ctl_->put_Bounds(rc);
      ctl_->put_IsVisible(TRUE);
      ComPtr<ICoreWebView2Settings> st;
      view_->get_Settings(st.put());
      if (st) {
        st->put_AreDevToolsEnabled(GetEnvironmentVariableW(L"DEIXION_DEVTOOLS", nullptr, 0) ? TRUE : FALSE);
        st->put_AreDefaultContextMenusEnabled(FALSE);
        st->put_IsStatusBarEnabled(FALSE);
        st->put_IsZoomControlEnabled(FALSE);
        st->put_IsBuiltInErrorPageEnabled(FALSE);
        st->put_AreDefaultScriptDialogsEnabled(FALSE);
        if (auto s3 = st.query<ICoreWebView2Settings3>(IidOf<ICoreWebView2Settings3>::get())) s3->put_AreBrowserAcceleratorKeysEnabled(GetEnvironmentVariableW(L"DEIXION_DEVTOOLS", nullptr, 0) ? TRUE : FALSE);
        if (auto s5 = st.query<ICoreWebView2Settings5>(IidOf<ICoreWebView2Settings5>::get())) s5->put_IsPinchZoomEnabled(FALSE);
        if (auto s6 = st.query<ICoreWebView2Settings6>(IidOf<ICoreWebView2Settings6>::get())) s6->put_IsSwipeNavigationEnabled(FALSE);
      }
      view_->add_WebMessageReceived(make_cb<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
                                        [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
                                          PWSTR w = nullptr;
                                          if (SUCCEEDED(a->get_WebMessageAsJson(&w)) && w) {
                                            const std::string s = utf8_from_pwstr(w);
                                            if (on_message_) on_message_(s);
                                          }
                                          return S_OK;
                                        }),
                                    &tok_msg_);
      view_->AddWebResourceRequestedFilter(L"https://app.deixion/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
      view_->add_WebResourceRequested(make_cb<ICoreWebView2WebResourceRequestedEventHandler, ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs*>(
                                          [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* a) -> HRESULT {
                                            ComPtr<ICoreWebView2WebResourceRequest> req;
                                            a->get_Request(req.put());
                                            PWSTR uri = nullptr;
                                            req->get_Uri(&uri);
                                            std::string u = utf8_from_pwstr(uri);
                                            std::string path = u.substr(u.find("//") + 2);
                                            path = path.substr(path.find('/'));
                                            if (const size_t q = path.find_first_of("?#"); q != std::string::npos) path.resize(q);
                                            if (path == "/") path = "/index.html";
                                            std::string mime;
                                            const void* data = nullptr;
                                            size_t size = 0;
                                            const bool ok = on_resource_ && on_resource_(path, mime, data, size);
                                            IStream* stream = ok ? SHCreateMemStream(static_cast<const BYTE*>(data), static_cast<UINT>(size)) : nullptr;
                                            const std::wstring hdr = ok ? L"Content-Type: " + text::widen(mime) +
                                                                              L"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'self'; "
                                                                              L"img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; font-src 'self' data:; script-src 'self'; connect-src 'self'\r\n"
                                                                        : L"Content-Type: text/plain\r\n";
                                            ComPtr<ICoreWebView2WebResourceResponse> resp;
                                            env_->CreateWebResourceResponse(stream, ok ? 200 : 404, ok ? L"OK" : L"Not Found", hdr.c_str(), resp.put());
                                            if (stream) stream->Release();
                                            a->put_Response(resp.get());
                                            return S_OK;
                                          }),
                                      &tok_res_);
      view_->add_NavigationCompleted(make_cb<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                                         [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                                           BOOL ok = FALSE;
                                           a->get_IsSuccess(&ok);
                                           if (on_nav_) on_nav_(ok != FALSE);
                                           return S_OK;
                                         }),
                                     &tok_nav_);
      view_->add_NewWindowRequested(make_cb<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
                                        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                                          a->put_Handled(TRUE);
                                          return S_OK;
                                        }),
                                    &tok_win_);
      view_->add_PermissionRequested(make_cb<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs*>(
                                         [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* a) -> HRESULT {
                                           a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                                           return S_OK;
                                         }),
                                     &tok_perm_);
      std::string ver;
      runtime_installed(&ver);
      ready_(true, ver);
      return S_OK;
    };
    return env_->CreateCoreWebView2Controller(host_, make_cb<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(on_ctl)) ;
  };
  const HRESULT hr = create_env(nullptr, user_dir.c_str(), nullptr,
                                make_cb<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(on_env));
  if (FAILED(hr)) ready_(false, "WebView2 runtime is not installed " + hr_text(hr));
}

void WebView::resize(const RECT& r) {
  if (ctl_) ctl_->put_Bounds(r);
}
void WebView::set_visible(bool v) {
  if (ctl_) ctl_->put_IsVisible(v ? TRUE : FALSE);
}
void WebView::set_background(u8 r, u8 g, u8 b) {
  if (!ctl_) return;
  if (auto c2 = ctl_.query<ICoreWebView2Controller2>(IidOf<ICoreWebView2Controller2>::get())) c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{255, r, g, b});
}
void WebView::post(const std::string& json) {
  if (view_) view_->PostWebMessageAsJson(text::widen(json).c_str());
}
void WebView::navigate(const std::wstring& url) {
  if (view_) view_->Navigate(url.c_str());
}
void WebView::focus() {
  if (ctl_) ctl_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
}

}  // namespace dx::app
