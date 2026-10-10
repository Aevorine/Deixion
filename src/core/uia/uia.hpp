#pragma once
#include <uiautomation.h>

#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/base/json.hpp"
#include "core/geo/meridian.hpp"
#include "core/win/com.hpp"

namespace dx::uia {

enum : u32 {
  P_INVOKE = 1,
  P_VALUE = 2,
  P_TOGGLE = 4,
  P_SELECT = 8,
  P_EXPAND = 16,
  P_SCROLL = 32,
  P_TEXT = 64,
  P_RANGE = 128,
  P_LEGACY = 256,
  P_SCROLLITEM = 512,
};
enum : u32 { F_ENABLED = 1, F_OFFSCREEN = 2, F_FOCUSED = 4, F_FOCUSABLE = 8 };

struct Node {
  i32 id{0};
  i32 parent{-1};
  i32 depth{0};
  i32 ctype{0};
  std::string role;
  std::string name;
  std::string aid;
  std::string cls;
  geo::RectI r;
  u32 flags{0};
  u32 patterns{0};
  u64 native{0};
  ComPtr<IUIAutomationElement> el;
};

struct Snapshot {
  u64 gen{0};
  u64 hwnd{0};
  geo::Frame frame;
  std::vector<Node> nodes;
  u64 built_unix_ms{0};
  u64 build_us{0};
  u64 total{0};
  u64 act{0};  // 构建时该窗口的界面事件计数，用来判断缓存是否可能过期
  bool truncated{false};
};

struct SnapOpts {
  u32 max_nodes{1500};
  bool include_offscreen{false};
  u32 ttl_ms{350};
  bool force{false};
};

struct FindQuery {
  std::string text;
  std::string role;
  std::string aid;
  bool interactive{false};
  bool enabled_only{false};
  int limit{5};
};

struct Match {
  int idx{-1};
  double score{0};
};

class Service {
 public:
  static Service& get();

  Res<std::shared_ptr<Snapshot>> snapshot(HWND h, const SnapOpts& o = {});
  std::shared_ptr<Snapshot> cached(HWND h, u32 max_age_ms);
  void invalidate(HWND h);
  void invalidate_all();

  static std::vector<Match> find(const Snapshot& s, const FindQuery& q);
  static int hit_test(const Snapshot& s, geo::PointI px, bool interactive_only = false);
  static Json node_json(const Snapshot& s, const Node& n);
  static Json can_list(u32 patterns);

  // 用前校验：取该元素 3 个实时属性核对缓存，一致才可信。
  static bool still_valid(const Node& n);

  Res<void> invoke(const Node& n);
  Res<void> toggle(const Node& n);
  Res<void> select(const Node& n);
  Res<void> expand(const Node& n, bool open);
  Res<void> set_value(const Node& n, const std::wstring& v);
  Res<std::string> get_value(const Node& n);
  Res<void> focus(const Node& n);
  bool has_focus(const Node& n);  // 实时查询：元素此刻是否持有键盘焦点（缓存里的标志可能已过期）
  Res<void> scroll_into_view(const Node& n);
  Res<void> default_action(const Node& n);
  Res<void> scroll(const Node& container, int h, int v);
  Res<void> set_range(const Node& n, double v);
  Res<double> get_range(const Node& n);
  Res<std::string> toggle_state(const Node& n);

  IUIAutomation* raw();

 private:
  Service() = default;
  Res<void> ensure();
  struct Entry {
    std::shared_ptr<Snapshot> snap;
    u64 mono_ms{0};
  };
  std::mutex mu_;
  std::unordered_map<u64, Entry> cache_;
  ComPtr<IUIAutomation> ui_;
  ComPtr<IUIAutomationCacheRequest> cr_;
  ComPtr<IUIAutomationCondition> cond_all_, cond_onscreen_;
  u64 next_gen_{1};
};

}  // namespace dx::uia
