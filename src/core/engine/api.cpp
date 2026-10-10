#include "core/base/clock.hpp"
#include "core/base/log.hpp"
#include "core/engine/engine.hpp"

namespace dx::eng {

const std::unordered_map<std::string, Engine::Fn>& Engine::table() {
  static const std::unordered_map<std::string, Fn> t = {
      {"ping", &Engine::q_ping},
      {"status", &Engine::q_status},
      {"perf", &Engine::q_perf},
      {"windows", &Engine::q_windows},
      {"capture", &Engine::q_capture},
      {"elements", &Engine::q_elements},
      {"find", &Engine::q_find},
      {"locate", &Engine::q_locate},
      {"geo", &Engine::q_geo},
      {"read", &Engine::q_read},
      {"click", &Engine::a_click},
      {"type", &Engine::a_type},
      {"set_value", &Engine::a_set_value},
      {"key", &Engine::a_key},
      {"scroll", &Engine::a_scroll},
      {"drag", &Engine::a_drag},
      {"window", &Engine::a_window},
      {"launch", &Engine::a_launch},
      {"wait", &Engine::a_wait},
      {"batch", &Engine::a_batch},
      {"rollback", &Engine::a_rollback},
      {"journal", &Engine::q_journal},
      {"journal.clear", &Engine::q_journal_clear},
      {"experience", &Engine::q_exp},
      {"experience.reset", &Engine::q_exp_reset},
      {"log", &Engine::q_log},
      {"settings.get", &Engine::q_settings_get},
      {"settings.set", &Engine::q_settings_set},
      {"stop", &Engine::q_stop},
  };
  return t;
}

Res<Json> Engine::call(std::string_view method, const Json& params) {
  const auto& tbl = table();
  auto it = tbl.find(std::string(method));
  if (it == tbl.end()) return fail(E_BAD_ARG, "unknown method: " + std::string(method));
  if (!started_) return fail(E_INTERNAL, "engine is not started");
  calls_.fetch_add(1, std::memory_order_relaxed);
  Stopwatch sw;
  // 引擎方法在管道服务线程、界面工作线程里被调用，这些线程里逃出来的异常会直接 std::terminate 掉整个应用（连同托盘与所有已连接的客户端）；
  // 在唯一入口把异常变成一个普通的失败结果。
  Res<Json> r = fail(E_INTERNAL, "unexpected failure");
  try {
    r = (this->*(it->second))(params.is_obj() ? params : Json::object());
  } catch (const std::exception& ex) {
    LOGE("api", "{} threw: {}", method, ex.what());
    r = fail(E_INTERNAL, std::string("internal error: ") + ex.what());
  } catch (...) {
    LOGE("api", "{} threw an unknown exception", method);
    r = fail(E_INTERNAL, "internal error");
  }
  record_perf(std::string(method), sw.ns());
  if (!r) {
    errors_.fetch_add(1, std::memory_order_relaxed);
    LOGD("api", "{} failed: {}", method, r.error().msg);
  }
  return r;
}

Json Engine::method_catalog() {
  Json a = Json::array();
  for (const auto& kv : table()) a.push(kv.first);
  return a;
}

}  // namespace dx::eng
