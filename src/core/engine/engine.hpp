#pragma once
#include <map>
#include <mutex>
#include <unordered_map>

#include "core/base/hist.hpp"
#include "core/base/json.hpp"
#include "core/capture/capture.hpp"
#include "core/engine/settings.hpp"
#include "core/geo/meridian.hpp"
#include "core/store/experience.hpp"
#include "core/store/journal.hpp"
#include "core/uia/uia.hpp"
#include "core/win/window.hpp"

namespace dx::eng {

class Engine {
 public:
  static Engine& get();

  Res<void> start();
  void shutdown();

  // 所有对外能力的唯一入口：界面、CLI、MCP、IPC 都走这里。
  Res<Json> call(std::string_view method, const Json& params);
  static Json method_catalog();

  void request_stop() { stop_epoch_.fetch_add(1); }
  u64 subscribe_actions(std::function<void(const Json&)> fn);
  void unsubscribe_actions(u64 token);
  bool started() const { return started_; }
  store::Experience& exp() { return exp_; }
  store::Journal& journal() { return journal_; }

 private:
  struct Target {
    HWND hwnd{nullptr};
    bool screen{false};
    geo::Frame frame;
    u32 pid{0};
    std::string app;
    std::string title;
  };
  struct PointRes {
    geo::PointI px;
    geo::LatLon ll;
    std::shared_ptr<uia::Snapshot> snap;
    int node{-1};
    std::string how;
  };
  struct Attempt {
    std::string name;
    std::function<Res<void>()> run;
    bool tail{false};  // 语义不同的兜底通道：不参与经验排序，只在排序内的通道都失败后才用
  };
  struct Outcome {
    bool ok{false};
    bool confirmed{false};
    std::string strategy;
    u64 us{0};
    int code{0};
    std::string err;
    store::Inverse inv;
    Json extra;
  };
  using Fn = Res<Json> (Engine::*)(const Json&);

  // —— core.cpp
  // allow_minimized：窗口操作（还原 / 关闭 / 移动）要能作用在最小化的窗口上，其它操作仍然要求先还原。
  Res<Target> target_of(const Json& p, bool required, bool allow_minimized = false);
  Res<PointRes> point_of(const Target& t, const Json& p, bool allow_hit);
  Outcome run_ladder(const Target& t, const std::string& action, const std::string& role, std::vector<Attempt> ladder, const Settings& st, bool verify_on, bool prelude_done = false);
  Json outcome_json(const Target& t, const PointRes* pr, const Outcome& o, u64 journal_id);
  u64 log_action(const std::string& method, const Json& params, const Target& t, const Outcome& o);
  Res<std::shared_ptr<uia::Snapshot>> snap_for(const Target& t, u32 ttl_ms, bool listing);
  Res<void> gate(const Settings& st) const;
  static Res<void> apply_via(std::vector<Attempt>& ladder, const Json& p);
  static bool msg_hostile(HWND top, geo::PointI px);
  bool fg_mode(const Settings& st, const Json& p) const;
  int speed_ms(const Settings& st) const;
  Json selector_of(const uia::Node& n, HWND h) const;
  Res<std::pair<std::shared_ptr<uia::Snapshot>, int>> resolve_selector(const Json& sel, u32 ttl_ms);
  Res<void> uia_activate(const uia::Node& n, store::Inverse* inv, HWND h);
  void fg_prelude(const Target& t, const PointRes* pr, const std::string& label, const Settings& st);

  // —— actions.cpp
  Res<Json> a_click(const Json& p);
  Res<Json> a_type(const Json& p);
  Res<Json> a_set_value(const Json& p);
  Res<Json> a_key(const Json& p);
  Res<Json> a_scroll(const Json& p);
  Res<Json> a_drag(const Json& p);
  Res<Json> a_window(const Json& p);
  Res<Json> a_launch(const Json& p);
  Res<Json> a_wait(const Json& p);
  Res<Json> a_rollback(const Json& p);
  Res<Json> a_batch(const Json& p);

  // —— queries.cpp
  Res<Json> q_windows(const Json& p);
  Res<Json> q_capture(const Json& p);
  Res<Json> q_elements(const Json& p);
  Res<Json> q_find(const Json& p);
  Res<Json> q_locate(const Json& p);
  Res<Json> q_geo(const Json& p);
  Res<Json> q_read(const Json& p);
  Res<Json> q_status(const Json& p);
  Res<Json> q_perf(const Json& p);
  Res<Json> q_exp(const Json& p);
  Res<Json> q_exp_reset(const Json& p);
  Res<Json> q_journal(const Json& p);
  Res<Json> q_journal_clear(const Json& p);
  Res<Json> q_log(const Json& p);
  Res<Json> q_settings_get(const Json& p);
  Res<Json> q_settings_set(const Json& p);
  Res<Json> q_stop(const Json& p);
  Res<Json> q_ping(const Json& p);

  void record_perf(const std::string& name, u64 ns);
  static const std::unordered_map<std::string, Fn>& table();

  store::Experience exp_;
  store::Journal journal_;
  std::mutex act_mu_;
  std::atomic<u64> stop_epoch_{0};
  std::mutex sub_mu_;
  std::vector<std::pair<u64, std::function<void(const Json&)>>> sub_actions_;
  u64 next_sub_{1};
  std::mutex hist_mu_;
  std::map<std::string, std::unique_ptr<Hist>> hist_;
  std::mutex shot_mu_;
  std::unordered_map<u64, cap::TileMap> last_tiles_;
  u64 started_ms_{0};
  std::atomic<u64> calls_{0}, errors_{0};
  bool started_{false};
};

}  // namespace dx::eng
