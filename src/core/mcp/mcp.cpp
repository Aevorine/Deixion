#include "core/mcp/mcp.hpp"

#include <windows.h>

#include <mutex>

#include "core/base/log.hpp"

namespace dx::mcp {
namespace {

Json P(const char* type, const char* desc) {
  Json j = Json::object();
  j.set("type", type).set("description", desc);
  return j;
}
Json Penum(std::initializer_list<const char*> vals, const char* desc) {
  Json j = P("string", desc);
  Json a = Json::array();
  for (const char* v : vals) a.push(v);
  j.set("enum", std::move(a));
  return j;
}
Json Schema(std::initializer_list<std::pair<const char*, Json>> props, std::initializer_list<const char*> required = {}) {
  Json s = Json::object();
  s.set("type", "object");
  Json pr = Json::object();
  for (const auto& kv : props) pr.set(kv.first, kv.second);
  s.set("properties", std::move(pr));
  if (required.size()) {
    Json r = Json::array();
    for (const char* x : required) r.push(x);
    s.set("required", std::move(r));
  }
  return s;
}

Json point_props() {
  Json j = Json::object();
  j.set("element", P("string", "Element id such as e12 from elements/screenshot(elements:true)."));
  Json find = Schema({{"text", P("string", "Fuzzy name match")}, {"role", P("string", "Button, Edit, MenuItem, ...")}, {"aid", P("string", "Exact AutomationId")}});
  find.set("description", "Find the element now (fuzzy) and use its centre.");
  j.set("find", std::move(find));
  j.set("at", P("string", "Meridian coordinate \"lam,phi\", both in [0,1], origin top-left of the window client area, e.g. \"0.48,0.13\"."));
  j.set("code", P("string", "Meridian code (1-6 chars), uses the centre of that cell."));
  j.set("px", Schema({{"x", P("integer", "client px")}, {"y", P("integer", "client px")}}));
  j.set("screen", Schema({{"x", P("integer", "screen px")}, {"y", P("integer", "screen px")}}));
  return j;
}

Json with_point(std::initializer_list<std::pair<const char*, Json>> extra, std::initializer_list<const char*> required = {}) {
  Json s = Schema(extra, required);
  for (const auto& kv : point_props().obj()) s["properties"].set(kv.first, kv.second);
  return s;
}

struct Tool {
  const char* name;
  const char* method;
  const char* desc;
  Json schema;
};

const Json kWindow = P("string", "Target window: title fragment, exe:name.exe, class:Name, hwnd:0x1A2B, active, or screen.");

const std::vector<Tool>& tools() {
  static const std::vector<Tool> t = [] {
    std::vector<Tool> v;
    v.push_back({"windows", "windows", "List top-level windows (handle, exe, title, client size). Start here to pick a target.", Schema({{"filter", P("string", "Substring of title/exe/class")}})});
    v.push_back({"screenshot", "capture",
                 "Capture a window (or the screen) as JPEG with a labelled Meridian grid: lines every 0.1, labels give lam (x, top) and phi (y, left), both 0..1. Read coordinates off the grid, "
                 "or set elements:true to draw numbered boxes (e12) on interactive controls. Zoom with region:{a,b} to refine; labels stay in full-window coordinates. Works on background windows.",
                 Schema({{"window", kWindow},
                         {"region", Schema({{"a", P("string", "top-left \"lam,phi\"")}, {"b", P("string", "bottom-right \"lam,phi\"")}})},
                         {"code", P("string", "Capture only this Meridian cell")},
                         {"grid", P("boolean", "Draw the grid (default true)")},
                         {"elements", P("boolean", "Box interactive elements with ids")},
                         {"marks", P("array", "[{at:\"lam,phi\",label}] markers to draw")},
                         {"max_dim", P("integer", "Longest image side in px (default 1568)")}})});
    v.push_back({"elements", "elements",
                 "List UI Automation elements of a window: id, role, name, centre @lam,phi, patterns [invoke,value,...]. Use ids/centres with click/type. Much cheaper and more exact than reading pixels.",
                 Schema({{"window", kWindow}, {"query", P("string", "Fuzzy name filter")}, {"role", P("string", "Role filter")}, {"interactive", P("boolean", "Only actionable (default when no filter)")},
                         {"limit", P("integer", "Max rows (default 150)")}, {"refresh", P("boolean", "Bypass the 350 ms cache")}},
                        {"window"})});
    v.push_back({"find", "find", "Fuzzy-find elements by name/role/automation id; returns ranked matches.", Schema({{"window", kWindow}, {"text", P("string", "Name")}, {"role", P("string", "Role")}, {"aid", P("string", "AutomationId")}, {"limit", P("integer", "Max matches")}}, {"window"})});
    v.push_back({"locate", "locate", "What is at this point? Returns the element, the child window class, and Meridian codes for it.", with_point({{"window", kWindow}})});
    v.push_back({"click", "click",
                 "Click. Background mode never moves the user's cursor or steals focus: it tries UI Automation patterns, then window messages. Give a target by element id, find, at (lam,phi), code, px or screen.",
                 with_point({{"window", kWindow}, {"button", Penum({"left", "right", "middle"}, "Default left")}, {"count", P("integer", "1-3 (2 = double click)")}, {"hover", P("boolean", "Move only, no click")}}, {"window"})});
    v.push_back({"type", "type", "Type text into the focused control, or into the target if one is given (it gets focus first). replace:true overwrites the whole value.",
                 with_point({{"window", kWindow}, {"text", P("string", "Text to enter")}, {"replace", P("boolean", "Replace existing content")}}, {"window", "text"})});
    v.push_back({"key", "key", "Press keys: \"enter\", \"ctrl+s\", \"f5\", or a space-separated sequence \"ctrl+a ctrl+c\". Modifier chords may need the user's brief-foreground fallback.",
                 Schema({{"window", kWindow}, {"keys", P("string", "Key chord(s)")}, {"repeat", P("integer", "Repeat count")}}, {"window", "keys"})});
    v.push_back({"scroll", "scroll", "Scroll at a point (default window centre). dy>0 scrolls down, dx>0 right, in wheel notches.", with_point({{"window", kWindow}, {"dy", P("integer", "Rows")}, {"dx", P("integer", "Columns")}}, {"window"})});
    v.push_back({"drag", "drag", "Drag from one point to another. from/to each accept the same target fields as click.",
                 Schema({{"window", kWindow}, {"from", Schema({})}, {"to", Schema({})}, {"button", P("string", "left|right|middle")}, {"steps", P("integer", "Intermediate moves")}}, {"window", "from", "to"})});
    v.push_back({"set_value", "set_value", "Set a field/slider value directly through UI Automation (no typing). Undoable.", with_point({{"window", kWindow}, {"value", P("string", "New value (number for sliders)")}}, {"window", "value"})});
    v.push_back({"read", "read", "Read the value/text/toggle state of an element.", with_point({{"window", kWindow}}, {"window"})});
    v.push_back({"window_op", "window", "Window management: focus (needs the user's permission), minimize, maximize, restore, close, move, resize, topmost.",
                 Schema({{"window", kWindow}, {"op", Penum({"focus", "minimize", "maximize", "restore", "close", "move", "resize", "topmost"}, "Operation")},
                         {"rect", Schema({{"x", P("integer", "screen px")}, {"y", P("integer", "screen px")}, {"w", P("integer", "px")}, {"h", P("integer", "px")}})}},
                        {"window", "op"})});
    v.push_back({"launch", "launch", "Start a program, document or URL (background: no focus steal). Returns pid and the window once it appears.",
                 Schema({{"path", P("string", "exe, document or URL")}, {"args", P("string", "Arguments")}, {"cwd", P("string", "Working directory")}, {"wait_window_ms", P("integer", "Wait for its window (default 3000)")}}, {"path"})});
    v.push_back({"wait", "wait", "Wait without fixed sleeps: for=settle (UI stopped changing), element (appears), gone (disappears), window (appears).",
                 Schema({{"for", Penum({"settle", "element", "gone", "window"}, "What to wait for")}, {"window", kWindow}, {"find", Schema({{"text", P("string", "Name")}, {"role", P("string", "Role")}, {"aid", P("string", "AutomationId")}})},
                         {"timeout_ms", P("integer", "Default 5000")}, {"quiet_ms", P("integer", "Settle window")}},
                        {"for"})});
    v.push_back({"batch", "batch",
                 "Run many steps in one call (one round trip, each step milliseconds). steps:[{do:\"click\",...},{do:\"type\",...},{do:\"wait\",for:\"settle\"}]. defaults are merged into every step, e.g. {window:\"Notepad\"}. Prefer this for any sequence.",
                 Schema({{"steps", P("array", "Array of {do:<tool method>, ...params}")}, {"defaults", P("object", "Params merged into each step")}, {"stop_on_error", P("boolean", "Default true")}}, {"steps"})});
    v.push_back({"undo", "rollback", "Roll back the last N reversible actions (value changes, toggles, window moves, typed text best-effort).", Schema({{"count", P("integer", "Default 1")}, {"id", P("integer", "Specific journal id")}})});
    v.push_back({"status", "status", "Engine status: mode (background/foreground), paused, learned strategy stats. detail=journal|experience|perf|log for more.",
                 Schema({{"detail", Penum({"journal", "experience", "perf", "log"}, "Extra section")}})});
    return v;
  }();
  return t;
}

std::mutex g_out;
void write_line(const Json& j) {
  std::string s = j.dump();
  s.push_back('\n');
  std::lock_guard lk(g_out);
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD w = 0;
  const char* p = s.data();
  size_t n = s.size();
  while (n) {
    if (!WriteFile(out, p, static_cast<DWORD>(std::min<size_t>(n, 1u << 20)), &w, nullptr) || !w) break;
    p += w;
    n -= w;
  }
}

Json rpc_error(const Json& id, int code, const std::string& msg) {
  Json j = Json::object();
  j.set("jsonrpc", "2.0").set("id", id).set("error", Json::object().set("code", code).set("message", msg));
  return j;
}
Json rpc_result(const Json& id, Json result) {
  Json j = Json::object();
  j.set("jsonrpc", "2.0").set("id", id).set("result", std::move(result));
  return j;
}

Json text_content(const std::string& s) {
  Json c = Json::object();
  c.set("type", "text").set("text", s);
  return c;
}

std::string hint_for(int code) {
  switch (code) {
    case E_NOT_FOUND: return " Hint: call windows/elements to see what exists, then retry.";
    case E_STALE: return " Hint: the UI changed; call elements (refresh:true) and use fresh ids.";
    case E_DENIED: return " Hint: this needs a permission only the user can grant in Deixion settings.";
    case E_BUSY: return " Hint: Deixion is paused; ask the user to resume it.";
    case E_UNSUPPORTED: return " Hint: try another target (an element id) or ask the user to allow the brief-foreground fallback.";
    default: return "";
  }
}

Json render(const std::string& tool, const Json& r) {
  Json content = Json::array();
  if (tool == "screenshot" && r["image"].is_obj()) {
    Json meta = Json::object();
    for (const auto& kv : r.obj())
      if (kv.first != "image" && kv.first != "ok") meta.set(kv.first, kv.second);
    meta.set("image_px", Json::object().set("w", r["image"]["w"]).set("h", r["image"]["h"]));
    content.push(text_content(meta.dump()));
    Json img = Json::object();
    img.set("type", "image").set("data", r["image"]["b64"]).set("mimeType", r["image"]["mime"]);
    content.push(std::move(img));
  } else if ((tool == "elements" || tool == "windows") && r["text"].is_str()) {
    std::string head = tool == "elements" ? std::to_string(r["count"].as_int()) + " of " + std::to_string(r["total"].as_int()) + " elements" + (r["truncated"].as_bool() ? " (truncated scan)" : "") + ", frame " +
                                                std::to_string(r["frame"]["w"].as_int()) + "x" + std::to_string(r["frame"]["h"].as_int()) + "\n"
                                          : std::to_string(r["count"].as_int()) + " windows\n";
    content.push(text_content(head + r["text"].as_str()));
  } else {
    content.push(text_content(r.dump()));
  }
  Json res = Json::object();
  res.set("content", std::move(content)).set("isError", false);
  return res;
}

Json handle_tool(const Call& call, const std::string& name, const Json& args) {
  const Tool* tool = nullptr;
  for (const auto& t : tools())
    if (name == t.name) tool = &t;
  Json res = Json::object();
  auto err = [&](const std::string& m) {
    Json c = Json::array();
    c.push(text_content(m));
    res.set("content", std::move(c)).set("isError", true);
    return res;
  };
  if (!tool) return err("unknown tool: " + name);
  Json params = args.is_obj() ? args : Json::object();
  if (name == "status") {
    auto st = call("status", params);
    if (!st) return err(st.error().msg + hint_for(st.error().code));
    const std::string d = params["detail"].as_str();
    Json out = *st;
    if (!d.empty()) {
      const char* m = d == "journal" ? "journal" : d == "experience" ? "experience" : d == "perf" ? "perf" : "log";
      Json p2 = Json::object();
      p2.set("limit", 30);
      auto x = call(m, p2);
      if (x) out.set(d, *x);
    }
    return render(name, out);
  }
  if (name == "screenshot") {
    if (!params.has("window")) params.set("window", "screen");
  }
  auto r = call(tool->method, params);
  if (!r) return err(r.error().msg + hint_for(r.error().code));
  return render(name, *r);
}

}  // namespace

std::string instructions() {
  return "Deixion drives Windows apps fast and, in background mode, without touching the user's cursor or focus.\n"
         "Coordinates: every window (and the screen) is a unit square. lam = horizontal 0..1, phi = vertical 0..1, origin top-left of the client area. "
         "A screenshot carries a labelled grid of these coordinates; the same (lam,phi) hits the same spot at any size or DPI. Meridian codes (1-6 chars) are the same position as a hierarchical short code.\n"
         "Workflow: windows -> elements (exact, cheap) or screenshot (see) -> click/type/key with element ids, find, or at. Batch sequences with batch and wait for=settle instead of sleeping. "
         "Typed text is never logged. Use undo to reverse value changes. The user's mode and fallback permissions are theirs to set; you cannot change them.";
}

Json tools_list() {
  Json arr = Json::array();
  for (const auto& t : tools()) {
    Json o = Json::object();
    o.set("name", t.name).set("description", t.desc).set("inputSchema", t.schema);
    arr.push(std::move(o));
  }
  return arr;
}

int run_stdio(const Call& call) {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  std::string buf;
  char chunk[1 << 16];
  bool eof = false;
  while (!eof) {
    size_t nl;
    while ((nl = buf.find('\n')) == std::string::npos && !eof) {
      DWORD got = 0;
      if (!ReadFile(in, chunk, sizeof chunk, &got, nullptr) || !got) {
        eof = true;
        break;
      }
      buf.append(chunk, got);
    }
    if (nl == std::string::npos) {
      if (buf.empty()) break;
      nl = buf.size();
    }
    std::string line = buf.substr(0, nl);
    buf.erase(0, std::min(buf.size(), nl + 1));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty()) continue;
    auto req = Json::parse(line);
    if (!req || !req->is_obj()) {
      write_line(rpc_error(Json(), -32700, "parse error"));
      continue;
    }
    const Json& rq = *req;
    const std::string method = rq["method"].as_str();
    const Json id = rq["id"];
    const bool is_notification = !rq.has("id");
    if (method == "initialize") {
      const std::string pv = rq["params"]["protocolVersion"].as_str();
      Json caps = Json::object();
      caps.set("tools", Json::object().set("listChanged", false));
      Json si = Json::object();
      si.set("name", "deixion").set("version", DX_VERSION);
      Json r = Json::object();
      const bool known = pv == "2025-06-18" || pv == "2025-03-26" || pv == "2024-11-05";
      r.set("protocolVersion", known ? pv : "2025-06-18").set("capabilities", std::move(caps)).set("serverInfo", std::move(si)).set("instructions", instructions());
      write_line(rpc_result(id, std::move(r)));
    } else if (method == "ping") {
      write_line(rpc_result(id, Json::object()));
    } else if (method == "tools/list") {
      write_line(rpc_result(id, Json::object().set("tools", tools_list())));
    } else if (method == "tools/call") {
      write_line(rpc_result(id, handle_tool(call, rq["params"]["name"].as_str(), rq["params"]["arguments"])));
    } else if (method == "notifications/cancelled") {
      (void)call("stop", Json::object());
    } else if (!is_notification) {
      write_line(rpc_error(id, -32601, "method not found: " + method));
    }
  }
  return 0;
}

}  // namespace dx::mcp
