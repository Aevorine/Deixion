#pragma once
#include <deque>

#include "core/store/logstore.hpp"

namespace dx::store {

struct Inverse {
  std::string kind;  // none | set_value | toggle | window_rect | window_state | backspace
  Json data;
  bool best_effort{false};
};

struct Entry {
  u64 id{0};
  u64 ts_ms{0};
  std::string method;
  Json params;
  std::string app;
  u64 hwnd{0};
  std::string title;
  std::string strategy;
  bool ok{false};
  bool confirmed{false};
  u64 us{0};
  std::string err;
  Inverse inv;
  bool undone{false};
};

// 操作日志与回滚栈。每条动作落盘；崩溃重启后从文件恢复，撤销栈仍在。
class Journal {
 public:
  Res<void> open(const std::filesystem::path& file);
  void close();
  u64 add(Entry e);
  void mark_undone(u64 id);
  std::vector<Entry> list(size_t limit, bool only_undoable) const;
  std::optional<Entry> get(u64 id) const;
  std::vector<Entry> undo_plan(size_t count) const;
  Res<void> clear();
  u64 count() const;
  u64 recovered_bytes() const { return log_.dropped_bytes(); }
  static Json to_json(const Entry& e);

 private:
  void compact_locked();
  mutable std::mutex mu_;
  LogStore log_;
  std::deque<Entry> items_;
  u64 next_id_{1};
};

}  // namespace dx::store
