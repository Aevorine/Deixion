#pragma once
#include <filesystem>
#include <functional>
#include <string>

#include "payload.hpp"

namespace dxsetup {

namespace fs = std::filesystem;

struct Options {
  fs::path dir;  // 已规整，末级目录名为 Deixion
  bool start_menu{true};
  bool desktop{false};
  bool autostart{false};
  bool claude{true};
  bool purge{false};
  bool update{false};
  bool relaunch{false};
};

enum class Fail { None, Dir, Payload, Write, Busy };

struct Result {
  bool ok{false};
  Fail why{Fail::None};
  std::wstring note;  // 非致命提示（缺运行库等），英文关键字，由界面翻译
};

using Progress = std::function<void(int pct, int stage)>;

enum Stage { StPrepare, StStop, StUnpack, StCommit, StRegister, StClaude, StDone };

Result install(const Payload& pl, const Options& o, const Progress& prog);
Result uninstall(const Options& o, const Progress& prog);

}  // namespace dxsetup
