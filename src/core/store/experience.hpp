#pragma once
#include <map>
#include <unordered_map>

#include "core/store/logstore.hpp"

namespace dx::store {

// 经验库：每个（应用，控件角色，动作，策略）一条“臂”，记录带遗忘的成功率与延迟。
// 选择用 UCB：score = mean + c·sqrt(ln N / n)，先验把成本低的通道排前面，数据多了由实测说话。
struct ArmStat {
  double n{0};        // 带遗忘的有效样本数
  double reward{0};   // 带遗忘的奖励和
  double succ{0};     // 带遗忘的成功数
  double lat_mean{0}; // 延迟均值（ms，指数滑动）
  double lat_var{0};
  u64 total{0};       // 历史总次数
  u64 last_ms{0};
};

struct ArmKey {
  std::string app, role, action, strat;
  std::string str() const { return app + "|" + role + "|" + action + "|" + strat; }
};

class Experience {
 public:
  Res<void> open(const std::filesystem::path& file);
  void close();

  void update(const ArmKey& k, bool success, double reward, double latency_ms);
  ArmStat get(const ArmKey& k) const;
  // 按 UCB 给候选策略排序（候选已按先验从优到劣给出）。
  std::vector<std::string> rank(const std::string& app, const std::string& role, const std::string& action, const std::vector<std::string>& candidates) const;
  double score(const ArmStat& s, double prior, double total_n) const;

  double get_num(const std::string& key, double dflt) const;
  void set_num(const std::string& key, double v);

  Json arms_json(const std::string& app_filter, size_t limit) const;
  Json summary_json() const;
  Res<void> reset();
  Json export_json() const;
  void flush();
  u64 recovered_bytes() const { return log_.dropped_bytes(); }
  u64 record_count() const { return log_.records(); }

 private:
  void write_arm_locked(const std::string& key, const ArmStat& s);
  void compact_locked();

  mutable std::mutex mu_;
  LogStore log_;
  std::unordered_map<std::string, ArmStat> arms_;
  std::map<std::string, double> nums_;
  std::unordered_map<std::string, bool> dirty_;
  u64 since_flush_{0};
};

}  // namespace dx::store
