#pragma once
#include <windows.h>

#include <functional>
#include <string>

#include "WebView2.h"
#include "core/base/json.hpp"
#include "core/win/com.hpp"

namespace dx::app {

// WebView2 的薄封装：环境与控制器创建、虚拟主机资源响应（界面文件打包进 exe）、网页 ⇄ 原生的 JSON 消息。
class WebView {
 public:
  using MessageFn = std::function<void(std::string_view json)>;
  using ResourceFn = std::function<bool(const std::string& path, std::string& mime, const void*& data, size_t& size)>;

  ~WebView();
  // 异步创建；完成后回调 ready(ok, runtime_version)。必须在带消息循环的 STA 线程调用。
  void create(HWND host, const std::wstring& user_data_dir, std::function<void(bool, std::string)> ready);
  void resize(const RECT& r);
  void set_visible(bool v);
  void set_background(u8 r, u8 g, u8 b);
  void post(const std::string& json);
  void close();
  void on_message(MessageFn fn) { on_message_ = std::move(fn); }
  void on_resource(ResourceFn fn) { on_resource_ = std::move(fn); }
  void on_navigated(std::function<void(bool)> fn) { on_nav_ = std::move(fn); }
  void navigate(const std::wstring& url);
  void focus();
  bool ready() const { return view_.get() != nullptr; }

  static bool runtime_installed(std::string* version = nullptr);

 private:
  friend struct WvAccess;
  ComPtr<ICoreWebView2Environment> env_;
  ComPtr<ICoreWebView2Controller> ctl_;
  ComPtr<ICoreWebView2> view_;
  HWND host_{nullptr};
  MessageFn on_message_;
  ResourceFn on_resource_;
  std::function<void(bool)> on_nav_;
  std::function<void(bool, std::string)> ready_;
  EventRegistrationToken tok_msg_{}, tok_res_{}, tok_nav_{}, tok_win_{}, tok_perm_{};
};

}  // namespace dx::app
