#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <json.hpp>
#include <mutex>
#include <numeric>
#include <psapi.h>
#include <span>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <winhttp.h>
#include <wrl/client.h>
namespace vl {
using json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;
inline double seconds() {
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
inline void check(HRESULT hr, const char *operation) {
  if (FAILED(hr)) {
    std::ostringstream s;
    s << operation << " failed (0x" << std::hex << static_cast<unsigned long>(hr) << ")";
    throw std::runtime_error(s.str());
  }
}
struct Handle {
  HANDLE h = nullptr;
  Handle() = default;
  explicit Handle(HANDLE v) : h(v) {}
  ~Handle() {
    if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
  operator HANDLE() const { return h; }
};
struct Com {
  Com() { check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM initialization"); }
  ~Com() { CoUninitialize(); }
};
inline std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                              nullptr, 0);
  if (!n)
    throw std::runtime_error("Invalid UTF-8");
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}
inline std::string utf8(const std::wstring &w) {
  if (w.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr,
                              nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}
inline fs::path executable_dir() {
  std::wstring s(32768, 0);
  auto n = GetModuleFileNameW(nullptr, s.data(), static_cast<DWORD>(s.size()));
  s.resize(n);
  return fs::path(s).parent_path();
}
inline fs::path root() {
  wchar_t v[32768];
  DWORD n = GetEnvironmentVariableW(L"VOICELOCK_DATA", v, 32768);
  if (n && n < 32768)
    return v;
  n = GetEnvironmentVariableW(L"LOCALAPPDATA", v, 32768);
  if (!n || n >= 32768)
    throw std::runtime_error("LOCALAPPDATA is missing");
  return fs::path(v) / L"VoiceLock";
}
inline json read_json(const fs::path &p) {
  std::ifstream f(p);
  if (!f)
    throw std::runtime_error("Missing file: " + utf8(p.wstring()));
  return json::parse(f);
}
inline void save_json(const fs::path &p, const json &j) {
  fs::create_directories(p.parent_path());
  auto tmp = p;
  tmp += L".tmp";
  {
    std::ofstream f(tmp, std::ios::binary);
    f << j.dump(2);
    if (!f)
      throw std::runtime_error("Cannot save settings/report");
  }
  if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Cannot replace settings/report");
}
inline json defaults() {
  return {{"model", "int8-full"}, {"threads", 4},        {"chunk_ms", 80},
          {"input_device", ""},   {"output_device", ""}, {"input_rate", 48000},
          {"output_rate", 48000}, {"gaming", true},      {"gain", 1.0},
          {"diagnostics", false}, {"output_buffer_ms",80}};
}
inline json validate(json s) {
  auto d = defaults();
  for (auto &[k, v] : s.items()) {
    if (!d.contains(k))
      throw std::runtime_error("Unknown setting: " + k);
    d[k] = v;
  }
  auto model = d.at("model").get<std::string>();
  int t = d.at("threads"), c = d.at("chunk_ms");
  double gain = d.at("gain");
  int cushion=d.at("output_buffer_ms");
  if(cushion!=40&&cushion!=80&&cushion!=120)throw std::runtime_error("Playback cushion must be 40, 80 or 120 ms");
  if ((model != "int8" && model != "int8-full") || t < 1 || t > 4 || (c != 40 && c != 80) ||
      !std::isfinite(gain) || gain < 0 || gain > 2)
    throw std::runtime_error("Invalid model, threads, chunk duration or gain");
  for (auto key : {"input_rate", "output_rate"}) {
    int r = d.at(key);
    if (r != 16000 && r != 44100 && r != 48000 && r != 96000)
      throw std::runtime_error("Unsupported sample rate");
  }
  d.at("gaming").get<bool>();
  d.at("diagnostics").get<bool>();
  d.at("input_device").get<std::string>();
  d.at("output_device").get<std::string>();
  return d;
}
inline std::string sha256(const fs::path &p) {
  BCRYPT_ALG_HANDLE alg{};
  BCRYPT_HASH_HANDLE hash{};
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
    throw std::runtime_error("SHA-256 unavailable");
  struct Cleanup {
    BCRYPT_ALG_HANDLE a;
    BCRYPT_HASH_HANDLE &h;
    ~Cleanup() {
      if (h)
        BCryptDestroyHash(h);
      BCryptCloseAlgorithmProvider(a, 0);
    }
  } cleanup{alg, hash};
  if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0)
    throw std::runtime_error("SHA-256 initialization failed");
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Model missing: " + utf8(p.wstring()) + ". Download models in Setup.");
  std::array<char, 65536> b;
  while (f) {
    f.read(b.data(), b.size());
    auto n = f.gcount();
    if (n && BCryptHashData(hash, reinterpret_cast<PUCHAR>(b.data()), static_cast<ULONG>(n), 0) < 0)
      throw std::runtime_error("Hash read failed");
  }
  std::array<unsigned char, 32> digest{};
  if (BCryptFinishHash(hash, digest.data(), 32, 0) < 0)
    throw std::runtime_error("Hash finalization failed");
  std::ostringstream s;
  for (auto c : digest)
    s << std::hex << std::setw(2) << std::setfill('0') << int(c);
  return s.str();
}
inline const std::string revision = "a50f4e1d109301d4b6f61dec57ebb1a61e7d8f03";
inline const std::map<std::string, std::string> model_hashes = {
    {"enroll.onnx", "86645cba23f207def0b31a6e74a615d3b7a813b4fdf957c53b3af2905ba2ffd3"},
    {"chunk_int8.onnx", "f0d2b0aaf95affe8894ea008d0105be4f5f5354f7244835534c31b1a7dd4699a"},
    {"chunk_int8_full.onnx", "7b57c1bfd5acd5b33768a9dff09d9af487d45374b3ba0b991e541ff121eaa337"}};
inline void verify_model(const fs::path &p) {
  if (sha256(p) != model_hashes.at(p.filename().string()))
    throw std::runtime_error("Model integrity check failed: " + p.filename().string() +
                             ". Download models again.");
}
struct Internet {
  HINTERNET h{};
  explicit Internet(HINTERNET v) : h(v) {
    if (!h)
      throw std::runtime_error("HTTPS connection failed");
  }
  ~Internet() { WinHttpCloseHandle(h); }
  operator HINTERNET() const { return h; }
};
inline void download(const std::wstring &url, const fs::path &dest) {
  URL_COMPONENTS c{};
  c.dwStructSize = sizeof(c);
  c.dwHostNameLength = c.dwUrlPathLength = c.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &c) || c.nScheme != INTERNET_SCHEME_HTTPS)
    throw std::runtime_error("Invalid HTTPS model URL");
  Internet session(WinHttpOpen(L"VoiceLockNative/2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  WinHttpSetTimeouts(session, 30000, 30000, 30000, 120000);
  std::wstring host(c.lpszHostName, c.dwHostNameLength), path(c.lpszUrlPath, c.dwUrlPathLength);
  if (c.dwExtraInfoLength)
    path.append(c.lpszExtraInfo, c.dwExtraInfoLength);
  Internet connection(WinHttpConnect(session, host.c_str(), c.nPort, 0));
  Internet request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                          0) ||
      !WinHttpReceiveResponse(request, nullptr))
    throw std::runtime_error("Model download failed; check internet connectivity");
  DWORD status = 0, n = sizeof(status);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX);
  if (status != 200)
    throw std::runtime_error("Model server returned HTTP " + std::to_string(status));
  std::ofstream f(dest, std::ios::binary);
  std::array<char, 65536> b;
  DWORD got = 0;
  uint64_t total = 0;
  for (;;) {
    if (!WinHttpReadData(request, b.data(), static_cast<DWORD>(b.size()), &got))
      throw std::runtime_error("Model download interrupted");
    if (!got)
      break;
    total += got;
    if (total > 256ull * 1024 * 1024)
      throw std::runtime_error("Model download exceeds expected size");
    f.write(b.data(), got);
    if (!f)
      throw std::runtime_error("Cannot write model cache");
  }
}
inline json setup_models() {
  auto dir = root() / "models" / "onnx";
  fs::create_directories(dir);
  for (auto &[name, hash] : model_hashes) {
    auto p = dir / name;
    if (fs::exists(p) && sha256(p) == hash)
      continue;
    auto tmp = p;
    tmp += L".download";
    download(
        wide("https://huggingface.co/arbazsiddiqui/voxlock/resolve/" + revision + "/onnx/" + name),
        tmp);
    if (sha256(tmp) != hash)
      throw std::runtime_error("Downloaded model hash mismatch");
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING))
      throw std::runtime_error("Cannot install model cache");
  }
  json j = {{"revision", revision}, {"files", model_hashes}};
  save_json(root() / "models" / "manifest.json", j);
  return j;
}
struct Wav {
  std::vector<float> samples;
  int rate = 16000;
};
inline Wav read_wav(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Missing WAV: " + utf8(p.wstring()));
  char head[12];
  f.read(head, 12);
  if (std::memcmp(head, "RIFF", 4) || std::memcmp(head + 8, "WAVE", 4))
    throw std::runtime_error("Expected RIFF WAV");
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  std::vector<char> data;
  while (f) {
    char id[4];
    uint32_t n = 0;
    f.read(id, 4);
    f.read(reinterpret_cast<char *>(&n), 4);
    if (!f)
      break;
    if (n > 512 * 1024 * 1024)
      throw std::runtime_error("WAV too large");
    if (!std::memcmp(id, "fmt ", 4)) {
      if (n < 16)
        throw std::runtime_error("Invalid WAV format");
      std::vector<char> b(n);
      f.read(b.data(), n);
      std::memcpy(&format, b.data(), 2);
      std::memcpy(&channels, b.data() + 2, 2);
      std::memcpy(&rate, b.data() + 4, 4);
      std::memcpy(&bits, b.data() + 14, 2);
      if (format == 0xfffe && n >= 40)
        std::memcpy(&format, b.data() + 24, 2);
    } else if (!std::memcmp(id, "data", 4)) {
      data.resize(n);
      f.read(data.data(), n);
    } else
      f.seekg(n, std::ios::cur);
    if (n % 2)
      f.seekg(1, std::ios::cur);
  }
  if (!channels || rate < 8000 || rate > 192000 ||
      !((format == 3 && bits == 32) || (format == 1 && bits == 16)))
    throw std::runtime_error("Use float32 or PCM16 WAV");
  size_t frame = channels * (bits / 8);
  Wav w;
  w.rate = static_cast<int>(rate);
  w.samples.resize(data.size() / frame);
  for (size_t i = 0; i < w.samples.size(); ++i) {
    double sum = 0;
    for (unsigned c = 0; c < channels; ++c) {
      const char *p0 = data.data() + i * frame + c * (bits / 8);
      if (format == 3) {
        float v;
        std::memcpy(&v, p0, 4);
        sum += v;
      } else {
        int16_t v;
        std::memcpy(&v, p0, 2);
        sum += v / 32768.0;
      }
    }
    w.samples[i] = static_cast<float>(sum / channels);
    if (!std::isfinite(w.samples[i]))
      throw std::runtime_error("Nonfinite WAV audio");
  }
  return w;
}
inline void write_wav(const fs::path &p, std::span<const float> x, int rate = 16000) {
  if (!p.parent_path().empty())
    fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  auto u16 = [&](uint16_t n) { f.write(reinterpret_cast<char *>(&n), 2); };
  auto u32 = [&](uint32_t n) { f.write(reinterpret_cast<char *>(&n), 4); };
  f.write("RIFF", 4);
  u32(36 + static_cast<uint32_t>(x.size() * 4));
  f.write("WAVEfmt ", 8);
  u32(16);
  u16(3);
  u16(1);
  u32(rate);
  u32(rate * 4);
  u16(4);
  u16(32);
  f.write("data", 4);
  u32(static_cast<uint32_t>(x.size() * 4));
  f.write(reinterpret_cast<const char *>(x.data()), x.size_bytes());
  if (!f)
    throw std::runtime_error("Cannot write WAV");
}
inline void validate_reference(std::span<const float> x) {
  if (x.size() < 40000)
    throw std::runtime_error("Record at least 2.5 seconds of clean speech");
  size_t active = 0, frames = x.size() / 160;
  for (size_t k = 0; k < frames; ++k) {
    double e = 0;
    for (size_t i = 0; i < 160; ++i) {
      float v = x[k * 160 + i];
      if (!std::isfinite(v) || std::abs(v) >= .995f)
        throw std::runtime_error("Enrollment clips or contains invalid audio. "
                                 "Rerecord at lower gain.");
      e += v * v;
    }
    if (e / 160 > .003 * .003)
      ++active;
  }
  if (active < frames * .2)
    throw std::runtime_error("Too little audible speech. Rerecord in a quiet room.");
}
inline double percentile(std::vector<double> x, double q) {
  if (x.empty())
    return 0;
  std::sort(x.begin(), x.end());
  double pos = (x.size() - 1) * q;
  size_t a = static_cast<size_t>(pos), b = std::min(a + 1, x.size() - 1);
  return x[a] + (x[b] - x[a]) * (pos - a);
}
inline double rss_mb() {
  PROCESS_MEMORY_COUNTERS m{};
  m.cb = sizeof(m);
  GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof(m));
  return m.WorkingSetSize / 1048576.;
}
inline double cpu_seconds() {
  FILETIME create, exit, kernel, user;
  GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
  ULARGE_INTEGER k, u;
  k.LowPart = kernel.dwLowDateTime;
  k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;
  return (k.QuadPart + u.QuadPart) / 1e7;
}
inline json hardware() {
  wchar_t cpu[256]{};
  DWORD size = sizeof(cpu);
  RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
               L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &size);
  MEMORYSTATUSEX m{sizeof(m)};
  GlobalMemoryStatusEx(&m);
  return {{"cpu", utf8(cpu)},
          {"logical_processors", std::thread::hardware_concurrency()},
          {"ram_gb", m.ullTotalPhys / 1073741824.},
          {"available_ram_gb", m.ullAvailPhys / 1073741824.},
          {"backend", "C++20 / WASAPI / ONNX Runtime CPU"}};
}
} // namespace vl
