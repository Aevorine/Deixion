#include "app/updater.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <fstream>

#include "core/base/log.hpp"
#include "core/base/paths.hpp"
#include "core/base/text.hpp"

#define DX_W2(x) L##x
#define DX_W(x) DX_W2(x)
#define DX_VERSION_W DX_W(DX_VERSION)

#ifndef DX_REPO
#define DX_REPO "Aevorine/Deixion"
#endif

namespace dx::app {
namespace {

struct Url {
  std::wstring host, path;
  INTERNET_PORT port{443};
  bool https{true};
};

bool crack(const std::string& u, Url& out) {
  const std::wstring w = text::widen(u);
  URL_COMPONENTSW uc{sizeof uc};
  wchar_t host[256], path[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  uc.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) return false;
  out.host.assign(host, uc.dwHostNameLength);
  out.path.assign(path, uc.dwUrlPathLength);
  if (uc.lpszExtraInfo && uc.dwExtraInfoLength) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  out.port = uc.nPort;
  out.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
  return true;
}

// 只信任 GitHub 的下载域名，且必须 https。
bool trusted_host(const std::wstring& h) {
  auto ends = [&](const wchar_t* s) {
    const std::wstring t(s);
    return h.size() >= t.size() && _wcsicmp(h.c_str() + h.size() - t.size(), t.c_str()) == 0;
  };
  return ends(L"github.com") || ends(L"githubusercontent.com");
}

Res<void> http_get(const std::string& url, std::string* body, const std::wstring* file, const std::function<void(u64, u64)>& progress) {
  Url u;
  if (!crack(url, u) || !u.https || !trusted_host(u.host)) return fail(E_DENIED, "untrusted update URL");
  HINTERNET ses = WinHttpOpen(L"Deixion/" DX_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
  if (!ses) return fail(E_IO, "cannot open network session");
  Defer c1([&] { WinHttpCloseHandle(ses); });
  WinHttpSetTimeouts(ses, 8000, 8000, 15000, 30000);
  HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
  if (!con) return fail(E_IO, "cannot connect");
  Defer c2([&] { WinHttpCloseHandle(con); });
  HINTERNET req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) return fail(E_IO, "cannot open request");
  Defer c3([&] { WinHttpCloseHandle(req); });
  const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
  if (!WinHttpSendRequest(req, hdr, static_cast<DWORD>(-1), nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr)) return fail(E_IO, "network request failed (" + std::to_string(GetLastError()) + ")");
  DWORD code = 0, sz = sizeof code;
  WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &sz, nullptr);
  if (code != 200) return fail(code == 404 ? E_NOT_FOUND : E_IO, "server answered " + std::to_string(code));
  u64 total = 0;
  wchar_t lenbuf[32];
  sz = sizeof lenbuf;
  if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, lenbuf, &sz, nullptr)) total = _wcstoui64(lenbuf, nullptr, 10);
  HANDLE fh = INVALID_HANDLE_VALUE;
  if (file) {
    fh = CreateFileW(file->c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) return fail(E_IO, "cannot write the download");
  }
  Defer c4([&] {
    if (fh != INVALID_HANDLE_VALUE) CloseHandle(fh);
  });
  std::vector<char> buf(1 << 16);
  u64 got = 0;
  for (;;) {
    DWORD n = 0;
    if (!WinHttpReadData(req, buf.data(), static_cast<DWORD>(buf.size()), &n)) return fail(E_IO, "download interrupted");
    if (!n) break;
    if (body) body->append(buf.data(), n);
    if (fh != INVALID_HANDLE_VALUE) {
      DWORD w = 0;
      if (!WriteFile(fh, buf.data(), n, &w, nullptr) || w != n) return fail(E_IO, "cannot write the download");
    }
    got += n;
    if (progress) progress(got, total);
    if (body && body->size() > (8u << 20)) return fail(E_BAD_ARG, "response too large");
  }
  return {};
}

Res<std::string> sha256_file(const std::wstring& path) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return fail(E_COM, "no SHA-256 provider");
  Defer c1([&] { BCryptCloseAlgorithmProvider(alg, 0); });
  BCRYPT_HASH_HANDLE h = nullptr;
  if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) < 0) return fail(E_COM, "hash init failed");
  Defer c2([&] { BCryptDestroyHash(h); });
  std::ifstream f(std::filesystem::path(path), std::ios::binary);
  if (!f) return fail(E_IO, "cannot read the download");
  std::vector<char> buf(1 << 16);
  while (f) {
    f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    const auto n = f.gcount();
    if (n > 0 && BCryptHashData(h, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(n), 0) < 0) return fail(E_COM, "hash failed");
  }
  u8 out[32];
  if (BCryptFinishHash(h, out, 32, 0) < 0) return fail(E_COM, "hash failed");
  return text::hex(out, 32);
}

std::vector<int> parts(const std::string& v) {
  std::vector<int> r;
  std::string cur;
  for (char c : v) {
    if (c >= '0' && c <= '9') cur.push_back(c);
    else if (c == '.' || c == '-' || c == '+') {
      if (!cur.empty()) r.push_back(std::atoi(cur.c_str()));
      cur.clear();
      if (c != '.') break;
    }
  }
  if (!cur.empty()) r.push_back(std::atoi(cur.c_str()));
  while (r.size() < 3) r.push_back(0);
  return r;
}
}  // namespace

Updater& Updater::get() {
  static Updater u;
  return u;
}

int Updater::compare_versions(const std::string& a, const std::string& b) {
  const auto x = parts(a), y = parts(b);
  for (size_t i = 0; i < 3; ++i)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

Json Updater::state() const {
  std::lock_guard lk(mu_);
  Json j = Json::object();
  j.set("state", state_).set("current", DX_VERSION).set("latest", latest_).set("notes", notes_).set("page", page_url_).set("error", error_);
  j.set("got", got_).set("total", total_).set("portable", paths::portable()).set("repo", DX_REPO);
  return j;
}

void Updater::publish() {
  if (notify_) notify_(state());
}

void Updater::set_error(const std::string& m) {
  {
    std::lock_guard lk(mu_);
    state_ = "error";
    error_ = m;
    busy_ = false;
  }
  publish();
}

void Updater::check(bool) {
  {
    std::lock_guard lk(mu_);
    if (busy_) return;
    busy_ = true;
    state_ = "checking";
    error_.clear();
    if (worker_.joinable()) worker_.join();
  }
  publish();
  worker_ = std::thread([this] {
    std::string body;
    auto r = http_get(std::string("https://api.github.com/repos/") + DX_REPO + "/releases/latest", &body, nullptr, nullptr);
    if (!r) {
      set_error(r.error().code == E_NOT_FOUND ? "no release has been published yet" : r.error().msg);
      return;
    }
    auto j = Json::parse(body);
    if (!j || !(*j)["tag_name"].is_str()) {
      set_error("unexpected response from GitHub");
      return;
    }
    {
      std::lock_guard lk(mu_);
      latest_ = (*j)["tag_name"].as_str();
      if (!latest_.empty() && (latest_[0] == 'v' || latest_[0] == 'V')) latest_.erase(0, 1);
      notes_ = (*j)["body"].as_str();
      page_url_ = (*j)["html_url"].as_str();
      asset_url_.clear();
      sums_url_.clear();
      for (const auto& a : (*j)["assets"].arr()) {
        const std::string n = a["name"].as_str();
        if (n == "Deixion-Setup-x64.exe") asset_url_ = a["browser_download_url"].as_str();
        if (n == "SHA256SUMS.txt") sums_url_ = a["browser_download_url"].as_str();
      }
      state_ = compare_versions(DX_VERSION, latest_) < 0 ? "available" : "current";
      busy_ = false;
    }
    LOGI("update", "latest release {} (current {})", latest_, DX_VERSION);
    publish();
  });
}

void Updater::download() {
  {
    std::lock_guard lk(mu_);
    if (busy_ || state_ != "available" || asset_url_.empty() || sums_url_.empty()) return;
    busy_ = true;
    state_ = "downloading";
    got_ = total_ = 0;
    if (worker_.joinable()) worker_.join();
  }
  publish();
  worker_ = std::thread([this] {
    std::string asset, sums;
    {
      std::lock_guard lk(mu_);
      asset = asset_url_;
      sums = sums_url_;
    }
    std::error_code ec;
    std::filesystem::create_directories(paths::update_dir(), ec);
    const std::wstring file = (paths::update_dir() / L"Deixion-Setup-x64.exe").wstring();
    u64 last_pub = 0;
    auto r = http_get(asset, nullptr, &file, [&](u64 got, u64 total) {
      {
        std::lock_guard lk(mu_);
        got_ = got;
        total_ = total;
      }
      if (got - last_pub > (256u << 10)) {
        last_pub = got;
        publish();
      }
    });
    if (!r) {
      set_error(r.error().msg);
      return;
    }
    std::string sbody;
    if (auto rs = http_get(sums, &sbody, nullptr, nullptr); !rs) {
      set_error("cannot fetch SHA256SUMS: " + rs.error().msg);
      return;
    }
    auto h = sha256_file(file);
    if (!h) {
      set_error(h.error().msg);
      return;
    }
    bool match = false;
    size_t pos = 0;
    while (pos < sbody.size()) {
      size_t e = sbody.find('\n', pos);
      if (e == std::string::npos) e = sbody.size();
      const std::string line = sbody.substr(pos, e - pos);
      pos = e + 1;
      if (line.find("Deixion-Setup-x64.exe") != std::string::npos && text::lower(line).starts_with(*h)) match = true;
    }
    if (!match) {
      std::filesystem::remove(file, ec);
      set_error("the downloaded installer does not match its published SHA-256; it was deleted");
      return;
    }
    {
      std::lock_guard lk(mu_);
      file_ = text::narrow(file);
      state_ = "ready";
      busy_ = false;
    }
    LOGI("update", "installer downloaded and verified ({})", *h);
    publish();
  });
}

Res<void> Updater::apply() {
  std::string file;
  {
    std::lock_guard lk(mu_);
    if (state_ != "ready") return fail(E_BUSY, "no verified update is ready");
    file = file_;
  }
  if (paths::portable()) return fail(E_UNSUPPORTED, "portable copies are not updated in place; download the new zip from the release page");
  std::wstring cmd = L"\"" + text::widen(file) + L"\" /S /UPDATE /RELAUNCH";
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) return fail(E_WIN32, "cannot start the installer");
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return {};
}

}  // namespace dx::app
