#include "core/uia/uia.hpp"

#include <algorithm>
#include <cmath>

#include "core/base/clock.hpp"
#include "core/base/text.hpp"
#include "core/win/window.hpp"

namespace dx::uia {
namespace {

const char* role_of(int ct) {
  switch (ct) {
    case 50000: return "Button";
    case 50001: return "Calendar";
    case 50002: return "CheckBox";
    case 50003: return "ComboBox";
    case 50004: return "Edit";
    case 50005: return "Link";
    case 50006: return "Image";
    case 50007: return "ListItem";
    case 50008: return "List";
    case 50009: return "Menu";
    case 50010: return "MenuBar";
    case 50011: return "MenuItem";
    case 50012: return "ProgressBar";
    case 50013: return "RadioButton";
    case 50014: return "ScrollBar";
    case 50015: return "Slider";
    case 50016: return "Spinner";
    case 50017: return "StatusBar";
    case 50018: return "Tab";
    case 50019: return "TabItem";
    case 50020: return "Text";
    case 50021: return "ToolBar";
    case 50022: return "ToolTip";
    case 50023: return "Tree";
    case 50024: return "TreeItem";
    case 50025: return "Custom";
    case 50026: return "Group";
    case 50027: return "Thumb";
    case 50028: return "DataGrid";
    case 50029: return "DataItem";
    case 50030: return "Document";
    case 50031: return "SplitButton";
    case 50032: return "Window";
    case 50033: return "Pane";
    case 50034: return "Header";
    case 50035: return "HeaderItem";
    case 50036: return "Table";
    case 50037: return "TitleBar";
    case 50038: return "Separator";
    default: return "Element";
  }
}

bool prop_bool(IUIAutomationElement* e, PROPERTYID id) {
  VARIANT v;
  VariantInit(&v);
  bool r = false;
  if (SUCCEEDED(e->GetCachedPropertyValue(id, &v)) && v.vt == VT_BOOL) r = v.boolVal != VARIANT_FALSE;
  VariantClear(&v);
  return r;
}

std::string cached_str(IUIAutomationElement* e, int which) {
  BSTR b = nullptr;
  HRESULT hr = S_OK;
  if (which == 0) hr = e->get_CachedName(&b);
  else if (which == 1) hr = e->get_CachedAutomationId(&b);
  else hr = e->get_CachedClassName(&b);
  std::string s = SUCCEEDED(hr) ? bstr_to_utf8(b) : std::string();
  SysFreeString(b);
  return s;
}

int code_stale(HRESULT hr) {
  return (hr == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE) || hr == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED) || hr == static_cast<HRESULT>(0x80131505) || hr == RPC_E_DISCONNECTED ||
          hr == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE))
             ? E_STALE
             : E_COM;
}

template <class P>
Res<ComPtr<P>> pattern_of(const Node& n, PATTERNID pid, REFIID iid, const char* what) {
  if (!n.el) return fail(E_STALE, "element is gone");
  ComPtr<P> p;
  const HRESULT hr = n.el->GetCurrentPatternAs(pid, iid, p.put_void());
  if (FAILED(hr)) return fail(code_stale(hr), std::string(what) + " pattern call failed " + hr_text(hr));
  if (!p) return fail(E_UNSUPPORTED, std::string("element does not support ") + what);
  return p;
}

}  // namespace

Service& Service::get() {
  static Service s;
  return s;
}

IUIAutomation* Service::raw() {
  if (!ensure()) return nullptr;
  return ui_.get();
}

Res<void> Service::ensure() {
  std::lock_guard lk(mu_);
  if (ui_) return {};
  com_init_thread();
  HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, ui_.put_void());
  if (FAILED(hr)) return fail(E_COM, "UI Automation is unavailable " + hr_text(hr));
  if (auto u6 = ui_.query<IUIAutomation6>(IID_IUIAutomation6)) {
    u6->put_ConnectionTimeout(2500);
    u6->put_TransactionTimeout(4000);
  }
  ui_->CreateCacheRequest(cr_.put());
  const PROPERTYID props[] = {UIA_ControlTypePropertyId,
                              UIA_NamePropertyId,
                              UIA_AutomationIdPropertyId,
                              UIA_ClassNamePropertyId,
                              UIA_BoundingRectanglePropertyId,
                              UIA_IsEnabledPropertyId,
                              UIA_IsOffscreenPropertyId,
                              UIA_HasKeyboardFocusPropertyId,
                              UIA_IsKeyboardFocusablePropertyId,
                              UIA_IsInvokePatternAvailablePropertyId,
                              UIA_IsValuePatternAvailablePropertyId,
                              UIA_IsTogglePatternAvailablePropertyId,
                              UIA_IsSelectionItemPatternAvailablePropertyId,
                              UIA_IsExpandCollapsePatternAvailablePropertyId,
                              UIA_IsScrollPatternAvailablePropertyId,
                              UIA_IsTextPatternAvailablePropertyId,
                              UIA_IsRangeValuePatternAvailablePropertyId,
                              UIA_IsLegacyIAccessiblePatternAvailablePropertyId,
                              UIA_IsScrollItemPatternAvailablePropertyId,
                              UIA_NativeWindowHandlePropertyId};
  for (PROPERTYID p : props) cr_->AddProperty(p);
  cr_->put_TreeScope(TreeScope_Element);
  cr_->put_AutomationElementMode(AutomationElementMode_Full);
  ui_->CreateTrueCondition(cond_all_.put());
  ComPtr<IUIAutomationCondition> ctrl, onscr;
  VARIANT t, f;
  VariantInit(&t);
  VariantInit(&f);
  t.vt = f.vt = VT_BOOL;
  t.boolVal = VARIANT_TRUE;
  f.boolVal = VARIANT_FALSE;
  ui_->CreatePropertyCondition(UIA_IsControlElementPropertyId, t, ctrl.put());
  ui_->CreatePropertyCondition(UIA_IsOffscreenPropertyId, f, onscr.put());
  ui_->CreateAndCondition(ctrl.get(), onscr.get(), cond_onscreen_.put());
  return {};
}

Res<std::shared_ptr<Snapshot>> Service::snapshot(HWND h, const SnapOpts& o) {
  if (auto r = ensure(); !r) return std::unexpected(r.error());
  if (!h || !IsWindow(h)) return fail(E_NOT_FOUND, "window is gone");
  const u64 key = reinterpret_cast<uintptr_t>(h);
  const u64 nowm = now_ns() / 1000000;
  if (!o.force) {
    std::lock_guard lk(mu_);
    auto it = cache_.find(key);
    if (it != cache_.end() && nowm - it->second.mono_ms <= o.ttl_ms) return it->second.snap;
  }
  Stopwatch sw;
  com_init_thread();
  ComPtr<IUIAutomationElement> root;
  HRESULT hr = ui_->ElementFromHandleBuildCache(h, cr_.get(), root.put());
  if (FAILED(hr) || !root) return fail(code_stale(hr), "cannot reach the window through UI Automation " + hr_text(hr));
  ComPtr<IUIAutomationElementArray> arr;
  hr = root->FindAllBuildCache(TreeScope_Descendants, o.include_offscreen ? cond_all_.get() : cond_onscreen_.get(), cr_.get(), arr.put());
  if (FAILED(hr)) return fail(code_stale(hr), "UI Automation search failed " + hr_text(hr));
  int len = 0;
  if (arr) arr->get_Length(&len);

  auto snap = std::make_shared<Snapshot>();
  snap->hwnd = key;
  snap->frame = win::client_frame(h);
  snap->total = static_cast<u64>(len);
  const int take = std::min<int>(len, static_cast<int>(o.max_nodes));
  snap->truncated = len > take;
  snap->nodes.reserve(static_cast<size_t>(take) + 1);

  auto fill = [&](IUIAutomationElement* e, Node& n) {
    CONTROLTYPEID ct = 0;
    e->get_CachedControlType(&ct);
    n.ctype = static_cast<i32>(ct);
    n.role = role_of(n.ctype);
    n.name = cached_str(e, 0);
    n.aid = cached_str(e, 1);
    n.cls = cached_str(e, 2);
    RECT rc{};
    e->get_CachedBoundingRectangle(&rc);
    n.r = {rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top};
    n.flags = 0;
    BOOL b = FALSE;
    if (SUCCEEDED(e->get_CachedIsEnabled(&b)) && b) n.flags |= F_ENABLED;
    if (SUCCEEDED(e->get_CachedIsOffscreen(&b)) && b) n.flags |= F_OFFSCREEN;
    if (SUCCEEDED(e->get_CachedHasKeyboardFocus(&b)) && b) n.flags |= F_FOCUSED;
    if (SUCCEEDED(e->get_CachedIsKeyboardFocusable(&b)) && b) n.flags |= F_FOCUSABLE;
    static const std::pair<PROPERTYID, u32> pm[] = {
        {UIA_IsInvokePatternAvailablePropertyId, P_INVOKE},          {UIA_IsValuePatternAvailablePropertyId, P_VALUE},
        {UIA_IsTogglePatternAvailablePropertyId, P_TOGGLE},          {UIA_IsSelectionItemPatternAvailablePropertyId, P_SELECT},
        {UIA_IsExpandCollapsePatternAvailablePropertyId, P_EXPAND},  {UIA_IsScrollPatternAvailablePropertyId, P_SCROLL},
        {UIA_IsTextPatternAvailablePropertyId, P_TEXT},              {UIA_IsRangeValuePatternAvailablePropertyId, P_RANGE},
        {UIA_IsLegacyIAccessiblePatternAvailablePropertyId, P_LEGACY}, {UIA_IsScrollItemPatternAvailablePropertyId, P_SCROLLITEM}};
    n.native = 0;
    {
      VARIANT nv;
      VariantInit(&nv);
      if (SUCCEEDED(e->GetCachedPropertyValue(UIA_NativeWindowHandlePropertyId, &nv)) && nv.vt == VT_I4) n.native = static_cast<u64>(static_cast<u32>(nv.lVal));
      VariantClear(&nv);
    }
    n.patterns = 0;
    for (const auto& [pid, bit] : pm)
      if (prop_bool(e, pid)) n.patterns |= bit;
  };

  {
    Node n;
    n.id = 0;
    n.depth = 0;
    n.el = root;
    fill(root.get(), n);
    if (n.r.w <= 0) n.r = snap->frame.r;
    snap->nodes.push_back(std::move(n));
  }
  std::vector<int> stack{0};
  for (int i = 0; i < take; ++i) {
    ComPtr<IUIAutomationElement> e;
    if (FAILED(arr->GetElement(i, e.put())) || !e) continue;
    Node n;
    n.id = static_cast<i32>(snap->nodes.size());
    n.el = e;
    fill(e.get(), n);
    if (n.r.w <= 0 || n.r.h <= 0) {
      if (n.name.empty() && n.aid.empty()) continue;
    }
    while (stack.size() > 1) {
      const auto& top = snap->nodes[static_cast<size_t>(stack.back())];
      const bool inside = n.r.x >= top.r.x - 1 && n.r.y >= top.r.y - 1 && n.r.right() <= top.r.right() + 1 && n.r.bottom() <= top.r.bottom() + 1;
      if (inside) break;
      stack.pop_back();
    }
    n.parent = stack.back();
    n.depth = static_cast<i32>(stack.size());
    stack.push_back(n.id);
    snap->nodes.push_back(std::move(n));
  }
  snap->build_us = sw.ns() / 1000;
  snap->built_unix_ms = unix_ms();
  {
    std::lock_guard lk(mu_);
    snap->gen = next_gen_++;
    cache_[key] = Entry{snap, nowm};
    if (cache_.size() > 24) {
      auto oldest = cache_.begin();
      for (auto it = cache_.begin(); it != cache_.end(); ++it)
        if (it->second.mono_ms < oldest->second.mono_ms) oldest = it;
      cache_.erase(oldest);
    }
  }
  return snap;
}

std::shared_ptr<Snapshot> Service::cached(HWND h, u32 max_age_ms) {
  const u64 nowm = now_ns() / 1000000;
  std::lock_guard lk(mu_);
  auto it = cache_.find(reinterpret_cast<uintptr_t>(h));
  return it != cache_.end() && nowm - it->second.mono_ms <= max_age_ms ? it->second.snap : nullptr;
}

void Service::invalidate(HWND h) {
  std::lock_guard lk(mu_);
  cache_.erase(reinterpret_cast<uintptr_t>(h));
}
void Service::invalidate_all() {
  std::lock_guard lk(mu_);
  cache_.clear();
}

std::vector<Match> Service::find(const Snapshot& s, const FindQuery& q) {
  std::vector<Match> out;
  const std::string role = text::lower(q.role);
  for (const auto& n : s.nodes) {
    if (n.id == 0) continue;
    if (!role.empty() && text::lower(n.role) != role) continue;
    if (q.enabled_only && !(n.flags & F_ENABLED)) continue;
    if (q.interactive && !(n.patterns & (P_INVOKE | P_VALUE | P_TOGGLE | P_SELECT | P_EXPAND | P_RANGE | P_LEGACY))) continue;
    double sc = 0;
    if (!q.aid.empty()) {
      if (n.aid == q.aid) sc = 1.0;
      else continue;
    }
    if (!q.text.empty()) {
      const double t = std::max(text::similarity(q.text, n.name), 0.9 * text::similarity(q.text, n.aid));
      if (t < 0.35) continue;
      sc = std::max(sc, t);
    } else if (q.aid.empty() && role.empty()) {
      continue;
    } else if (sc == 0) {
      sc = 0.5;
    }
    if (n.flags & F_ENABLED) sc += 0.01;
    if (n.patterns & P_INVOKE) sc += 0.005;
    out.push_back({n.id, sc});
  }
  std::stable_sort(out.begin(), out.end(), [](const Match& a, const Match& b) { return a.score > b.score; });
  if (q.limit > 0 && out.size() > static_cast<size_t>(q.limit)) out.resize(static_cast<size_t>(q.limit));
  return out;
}

int Service::hit_test(const Snapshot& s, geo::PointI px, bool interactive_only) {
  int best = 0;
  i64 best_area = INT64_MAX;
  for (const auto& n : s.nodes) {
    if (n.id == 0 || !n.r.contains(px.x, px.y)) continue;
    const bool inter = (n.patterns & (P_INVOKE | P_VALUE | P_TOGGLE | P_SELECT | P_EXPAND | P_RANGE)) != 0;
    if (interactive_only && (!inter || !(n.flags & F_ENABLED) || (n.flags & F_OFFSCREEN))) continue;
    const i64 a = n.r.area();
    if (a < best_area || (a == best_area && n.depth > s.nodes[static_cast<size_t>(best)].depth)) {
      best = n.id;
      best_area = a;
    }
  }
  return best;
}

Json Service::can_list(u32 p) {
  Json a = Json::array();
  if (p & P_INVOKE) a.push("invoke");
  if (p & P_VALUE) a.push("value");
  if (p & P_TOGGLE) a.push("toggle");
  if (p & P_SELECT) a.push("select");
  if (p & P_EXPAND) a.push("expand");
  if (p & P_SCROLL) a.push("scroll");
  if (p & P_TEXT) a.push("text");
  if (p & P_RANGE) a.push("range");
  return a;
}

Json Service::node_json(const Snapshot& s, const Node& n) {
  Json j = Json::object();
  j.set("id", "e" + std::to_string(n.id)).set("role", n.role);
  if (!n.name.empty()) j.set("name", n.name);
  if (!n.aid.empty()) j.set("aid", n.aid);
  const geo::LatLon c = geo::from_px(s.frame, n.r.center());
  j.set("at", geo::fmt(c));
  const geo::LatLon a = geo::from_px(s.frame, {n.r.x, n.r.y});
  const geo::LatLon b = geo::from_px(s.frame, {n.r.right() - 1, n.r.bottom() - 1});
  Json box = Json::array();
  for (double v : {a.lam, a.phi, b.lam, b.phi}) box.push(std::round(v * 10000.0) / 10000.0);
  j.set("box", std::move(box));
  j.set("code", geo::code_of(c, geo::level_for({n.r, s.frame.dpi, 0}) ));
  if (n.patterns) j.set("can", can_list(n.patterns));
  Json st = Json::array();
  if (!(n.flags & F_ENABLED)) st.push("disabled");
  if (n.flags & F_FOCUSED) st.push("focused");
  if (n.flags & F_OFFSCREEN) st.push("offscreen");
  if (st.size()) j.set("state", std::move(st));
  j.set("depth", n.depth);
  return j;
}

bool Service::still_valid(const Node& n) {
  if (!n.el) return false;
  BSTR b = nullptr;
  if (FAILED(n.el->get_CurrentName(&b))) return false;
  const std::string name = bstr_to_utf8(b);
  SysFreeString(b);
  if (name != n.name) return false;
  RECT rc{};
  if (FAILED(n.el->get_CurrentBoundingRectangle(&rc))) return false;
  if (std::abs(rc.left - n.r.x) > 2 || std::abs(rc.top - n.r.y) > 2 || std::abs((rc.right - rc.left) - n.r.w) > 2 || std::abs((rc.bottom - rc.top) - n.r.h) > 2) return false;
  BOOL en = FALSE;
  if (FAILED(n.el->get_CurrentIsEnabled(&en))) return false;
  return (en != FALSE) == ((n.flags & F_ENABLED) != 0);
}

Res<void> Service::invoke(const Node& n) {
  auto p = pattern_of<IUIAutomationInvokePattern>(n, UIA_InvokePatternId, IID_IUIAutomationInvokePattern, "invoke");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = (*p)->Invoke();
  if (FAILED(hr)) return fail(code_stale(hr), "invoke failed " + hr_text(hr));
  return {};
}
Res<void> Service::toggle(const Node& n) {
  auto p = pattern_of<IUIAutomationTogglePattern>(n, UIA_TogglePatternId, IID_IUIAutomationTogglePattern, "toggle");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = (*p)->Toggle();
  if (FAILED(hr)) return fail(code_stale(hr), "toggle failed " + hr_text(hr));
  return {};
}
Res<std::string> Service::toggle_state(const Node& n) {
  auto p = pattern_of<IUIAutomationTogglePattern>(n, UIA_TogglePatternId, IID_IUIAutomationTogglePattern, "toggle");
  if (!p) return std::unexpected(p.error());
  ToggleState st = ToggleState_Off;
  (*p)->get_CurrentToggleState(&st);
  return std::string(st == ToggleState_On ? "on" : st == ToggleState_Off ? "off" : "indeterminate");
}
Res<void> Service::select(const Node& n) {
  auto p = pattern_of<IUIAutomationSelectionItemPattern>(n, UIA_SelectionItemPatternId, IID_IUIAutomationSelectionItemPattern, "select");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = (*p)->Select();
  if (FAILED(hr)) return fail(code_stale(hr), "select failed " + hr_text(hr));
  return {};
}
Res<void> Service::expand(const Node& n, bool open) {
  auto p = pattern_of<IUIAutomationExpandCollapsePattern>(n, UIA_ExpandCollapsePatternId, IID_IUIAutomationExpandCollapsePattern, "expand");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = open ? (*p)->Expand() : (*p)->Collapse();
  if (FAILED(hr)) return fail(code_stale(hr), "expand failed " + hr_text(hr));
  return {};
}
Res<void> Service::set_value(const Node& n, const std::wstring& v) {
  auto p = pattern_of<IUIAutomationValuePattern>(n, UIA_ValuePatternId, IID_IUIAutomationValuePattern, "value");
  if (!p) return std::unexpected(p.error());
  BOOL ro = FALSE;
  (*p)->get_CurrentIsReadOnly(&ro);
  if (ro) return fail(E_DENIED, "element is read-only");
  Bstr b(v);
  const HRESULT hr = (*p)->SetValue(b.get());
  if (FAILED(hr)) return fail(code_stale(hr), "set value failed " + hr_text(hr));
  return {};
}
Res<std::string> Service::get_value(const Node& n) {
  if (n.patterns & P_VALUE) {
    auto p = pattern_of<IUIAutomationValuePattern>(n, UIA_ValuePatternId, IID_IUIAutomationValuePattern, "value");
    if (p) {
      BSTR b = nullptr;
      if (SUCCEEDED((*p)->get_CurrentValue(&b))) {
        std::string s = bstr_to_utf8(b);
        SysFreeString(b);
        return s;
      }
    }
  }
  if (n.patterns & P_TEXT) {
    auto p = pattern_of<IUIAutomationTextPattern>(n, UIA_TextPatternId, IID_IUIAutomationTextPattern, "text");
    if (p) {
      ComPtr<IUIAutomationTextRange> r;
      if (SUCCEEDED((*p)->get_DocumentRange(r.put())) && r) {
        BSTR b = nullptr;
        if (SUCCEEDED(r->GetText(1 << 16, &b))) {
          std::string s = bstr_to_utf8(b);
          SysFreeString(b);
          return s;
        }
      }
    }
  }
  return fail(E_UNSUPPORTED, "element exposes neither value nor text");
}
Res<void> Service::focus(const Node& n) {
  if (!n.el) return fail(E_STALE, "element is gone");
  const HRESULT hr = n.el->SetFocus();
  if (FAILED(hr)) return fail(code_stale(hr), "focus failed " + hr_text(hr));
  return {};
}
Res<void> Service::scroll_into_view(const Node& n) {
  auto p = pattern_of<IUIAutomationScrollItemPattern>(n, UIA_ScrollItemPatternId, IID_IUIAutomationScrollItemPattern, "scroll item");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = (*p)->ScrollIntoView();
  if (FAILED(hr)) return fail(code_stale(hr), "scroll into view failed " + hr_text(hr));
  return {};
}
Res<void> Service::default_action(const Node& n) {
  auto p = pattern_of<IUIAutomationLegacyIAccessiblePattern>(n, UIA_LegacyIAccessiblePatternId, IID_IUIAutomationLegacyIAccessiblePattern, "legacy");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = (*p)->DoDefaultAction();
  if (FAILED(hr)) return fail(code_stale(hr), "default action failed " + hr_text(hr));
  return {};
}
Res<void> Service::scroll(const Node& c, int h, int v) {
  auto p = pattern_of<IUIAutomationScrollPattern>(c, UIA_ScrollPatternId, IID_IUIAutomationScrollPattern, "scroll");
  if (!p) return std::unexpected(p.error());
  auto amt = [](int d) { return d == 0 ? ScrollAmount_NoAmount : d > 0 ? (d > 1 ? ScrollAmount_LargeIncrement : ScrollAmount_SmallIncrement) : (d < -1 ? ScrollAmount_LargeDecrement : ScrollAmount_SmallDecrement); };
  const HRESULT hr = (*p)->Scroll(amt(h), amt(v));
  if (FAILED(hr)) return fail(code_stale(hr), "scroll failed " + hr_text(hr));
  return {};
}
Res<void> Service::set_range(const Node& n, double v) {
  auto p = pattern_of<IUIAutomationRangeValuePattern>(n, UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, "range");
  if (!p) return std::unexpected(p.error());
  const HRESULT hr = (*p)->SetValue(v);
  if (FAILED(hr)) return fail(code_stale(hr), "set range failed " + hr_text(hr));
  return {};
}

}  // namespace dx::uia
