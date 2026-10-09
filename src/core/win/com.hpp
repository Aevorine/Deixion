#pragma once
#include <objbase.h>
#include <windows.h>

#include <string>

#include "core/base/types.hpp"

namespace dx {

template <class T>
class ComPtr {
 public:
  ComPtr() = default;
  explicit ComPtr(T* p) : p_(p) {}
  ComPtr(const ComPtr& o) : p_(o.p_) {
    if (p_) p_->AddRef();
  }
  ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
  ComPtr& operator=(const ComPtr& o) {
    if (this != &o) {
      if (o.p_) o.p_->AddRef();
      reset();
      p_ = o.p_;
    }
    return *this;
  }
  ComPtr& operator=(ComPtr&& o) noexcept {
    if (this != &o) {
      reset();
      p_ = o.p_;
      o.p_ = nullptr;
    }
    return *this;
  }
  ~ComPtr() { reset(); }

  void reset() {
    if (p_) {
      p_->Release();
      p_ = nullptr;
    }
  }
  T* get() const { return p_; }
  T* operator->() const { return p_; }
  T** put() {
    reset();
    return &p_;
  }
  void** put_void() { return reinterpret_cast<void**>(put()); }
  explicit operator bool() const { return p_ != nullptr; }
  T* detach() {
    T* t = p_;
    p_ = nullptr;
    return t;
  }

  template <class U>
  ComPtr<U> query(REFIID iid) const {
    ComPtr<U> out;
    if (p_) p_->QueryInterface(iid, out.put_void());
    return out;
  }

 private:
  T* p_{nullptr};
};

// 每个会用 COM 的线程调用一次，退出时自动反初始化。
void com_init_thread();

class Bstr {
 public:
  explicit Bstr(const std::wstring& s) : b_(SysAllocStringLen(s.data(), static_cast<UINT>(s.size()))) {}
  ~Bstr() { SysFreeString(b_); }
  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
  BSTR get() const { return b_; }

 private:
  BSTR b_;
};

std::string bstr_to_utf8(BSTR b);
std::string hr_text(HRESULT hr);

}  // namespace dx
