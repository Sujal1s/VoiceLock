#include "audio.hpp"
#include <sddl.h>
using namespace vl;
static json benchmark(const json &s, std::span<const float> reference, std::span<const float> audio,
                      const fs::path &out = {}) {
  Model model(s, reference);
  std::vector<float> result;
  result.reserve(audio.size() + 512);
  std::vector<double> times;
  size_t chunk = s.at("chunk_ms").get<int>() * 16;
  double begin = seconds(), cpu = cpu_seconds(), peak = rss_mb();
  for (size_t i = 0; i < audio.size(); i += chunk) {
    double start = seconds();
    auto y = model.push(audio.subspan(i, std::min(chunk, audio.size() - i)));
    times.push_back((seconds() - start) * 1000);
    result.insert(result.end(), y.begin(), y.end());
    if (times.size() % 100 == 0)
      peak = std::max(peak, rss_mb());
  }
  auto tail = model.flush();
  result.insert(result.end(), tail.begin(), tail.end());
  if (result.size() < audio.size())
    throw std::runtime_error("Model output was shorter than input");
  result.resize(audio.size());
  double elapsed = seconds() - begin, p95 = percentile(times, .95);
  uint64_t clips = 0;
  for (float v : result) {
    if (!std::isfinite(v))
      throw std::runtime_error("Invalid output WAV");
    if (std::abs(v) >= 1)
      ++clips;
  }
  if (!out.empty())
    write_wav(out, result);
  return {{"backend", "C++20 native"},
          {"settings", s},
          {"seconds", audio.size() / 16000.},
          {"wall_seconds", elapsed},
          {"rtf", elapsed / (audio.size() / 16000.)},
          {"p50_ms", percentile(times, .5)},
          {"p95_ms", p95},
          {"p99_ms", percentile(times, .99)},
          {"rtf_p95", p95 / s.at("chunk_ms").get<double>()},
          {"inference_deadline_misses",
           std::count_if(times.begin(), times.end(),
                         [&](double t) { return t > s.at("chunk_ms").get<double>(); })},
          {"cpu_percent", (cpu_seconds() - cpu) / elapsed /
                              std::max(1u, std::thread::hardware_concurrency()) * 100},
          {"peak_ram_mb", peak},
          {"output_clipped_samples", clips},
          {"estimated_model_latency_ms", s.at("chunk_ms").get<double>() + 24 + p95},
          {"actual_loopback_latency_ms", nullptr},
          {"audio_dropouts", nullptr}};
}
static json calibrate(const fs::path &reference_path, int duration) {
  if (duration < 3 || duration > 120)
    throw std::runtime_error("Calibration duration must be 3–120 seconds per configuration");
  auto ref = audio16(reference_path);
  validate_reference(ref);
  std::vector<float> audio(duration * 16000);
  for (size_t i = 0; i < audio.size(); ++i)
    audio[i] = ref[i % ref.size()];
  json results = json::array(), profiles = json::object();
  for (auto precision : {"int8-full", "int8"})
    for (int chunk : {80, 40})
      for (int threads : {1, 2, 3, 4}) {
        auto s = defaults();
        s["model"] = precision;
        s["threads"] = threads;
        s["chunk_ms"] = chunk;
        auto r = benchmark(s, ref, audio);
        r["qualified"] = r["rtf_p95"].get<double>() < .8 && r["rtf"].get<double>() < .8 &&
                         r["inference_deadline_misses"] == 0;
        results.push_back(r);
        save_json(root() / "calibration-native-progress.json",
                  {{"completed", results.size()}, {"total", 16}, {"last", r}});
      }
  for (auto name : {"Maximum Isolation", "Gaming Optimized", "Low Latency"}) {
    const json *best = nullptr;
    for (auto &r : results) {
      if (!r["qualified"].get<bool>() ||
          (std::string(name) == "Low Latency" && r["settings"]["chunk_ms"] != 40))
        continue;
      auto rank = [&](const json &v) {
        double quality = v["settings"]["model"] == "int8-full" ? 0 : 10000;
        double speed = v[std::string(name) == "Gaming Optimized" ? "cpu_percent" : "p95_ms"];
        return (std::string(name) == "Low Latency" ? 0 : quality) + speed;
      };
      if (!best || rank(r) < rank(*best))
        best = &r;
    }
    if (best)
      profiles[name] = best->at("settings");
  }
  json report = {
      {"backend", "C++20 native"},
      {"hardware", hardware()},
      {"workload", "Repeated clean reference; provisional synthetic "
                   "throughput, not a gaming or acoustic test"},
      {"results", results},
      {"profiles", profiles},
      {"warning", profiles.empty()
                      ? "No configuration qualified. Manual settings remain experimental."
                      : "Short benchmark; validate during your actual workload."}};
  save_json(root() / "calibration-native.json", report);
  return report;
}
class Service {
  json settings;
  std::unique_ptr<Engine> engine;
  double last_log = 0;
  void stop() { engine.reset(); }

public:
  Service()
      : settings(fs::exists(root() / "settings.json")
                     ? validate(read_json(root() / "settings.json"))
                     : defaults()) {}
  json call(const json &r) {
    std::string op = r.at("op");
    if (op == "hello") {
      json profiles = json::array();
      try {
        auto report = read_json(root() / "calibration-native.json");
        for (auto &[name, value] : report.at("profiles").items())
          profiles.push_back(name);
      } catch (const std::exception &) {
      }
      return {{"protocol", 1},
              {"backend", "C++20 native"},
              {"settings", settings},
              {"hardware", hardware()},
              {"enrolled", fs::exists(root() / "enrollment.wav")},
              {"models", fs::exists(root() / "models" / "manifest.json")},
              {"data_dir", utf8(root().wstring())},
              {"profiles", profiles}};
    }
    if (op == "devices")
      return device_json();
    if (op == "status") {
      auto value = engine ? engine->snapshot()
                          : json{{"running", false}, {"error", ""}, {"backend", "C++20 native"}};
      if (settings.at("diagnostics").get<bool>() && !settings.at("gaming").get<bool>() &&
          !value.value("economy", false) && seconds() - last_log > 5) {
        try {
          fs::create_directories(root());
          auto path = root() / "diagnostics-native.jsonl";
          if (fs::exists(path) && fs::file_size(path) > 1048576)
            MoveFileExW(path.c_str(), (root() / "diagnostics-native.previous.jsonl").c_str(),
                        MOVEFILE_REPLACE_EXISTING);
          std::ofstream file(path, std::ios::app);
          file << value.dump() << '\n';
          last_log = seconds();
        } catch (const std::exception &) {
        }
      }
      return value;
    }
    if (op == "stop") {
      stop();
      return {{"running", false}};
    }
    if (op == "settings") {
      auto s = validate(r.at("settings"));
      stop();
      save_json(root() / "settings.json", s);
      settings = s;
      return settings;
    }
    if (op == "setup") {
      stop();
      return setup_models();
    }
    if (op == "enroll") {
      stop();
      return enroll(settings, r.value("consent", false));
    }
    if (op == "forget") {
      stop();
      fs::remove(root() / "enrollment.wav");
      return {{"enrolled", false}};
    }
    if (op == "calibrate") {
      stop();
      auto report = calibrate(root() / "enrollment.wav", r.value("seconds", 15));
      if (report.at("profiles").contains("Gaming Optimized")) {
        auto selected = report["profiles"]["Gaming Optimized"];
        for (auto key : {"model", "threads", "chunk_ms"})
          settings[key] = selected[key];
        save_json(root() / "settings.json", settings);
      }
      return report;
    }
    if (op == "profile") {
      auto report = read_json(root() / "calibration-native.json");
      std::string name = r.at("name");
      if (!report.at("profiles").contains(name))
        throw std::runtime_error("This profile has no passing native benchmark. Calibrate first.");
      stop();
      for (auto key : {"model", "threads", "chunk_ms"})
        settings[key] = report["profiles"][name][key];
      settings["gaming"] = name == "Gaming Optimized";
      save_json(root() / "settings.json", settings);
      return settings;
    }
    if (op == "start") {
      stop();
      auto next = std::make_unique<Engine>(settings);
      next->start();
      engine = std::move(next);
      return engine->snapshot();
    }
    throw std::runtime_error("Unknown operation: " + op);
  }
};
static void serve(const std::string &name) {
  if (name.empty() || name.size() > 120 ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
          std::string::npos)
    throw std::runtime_error("Invalid pipe name");
  Handle token;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h))
    throw std::runtime_error("Cannot identify pipe owner");
  DWORD bytes = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
  std::vector<unsigned char> buffer(bytes);
  if (!GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes))
    throw std::runtime_error("Cannot read pipe owner");
  LPWSTR sid{};
  if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid))
    throw std::runtime_error("Cannot encode pipe owner");
  std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
  LocalFree(sid);
  PSECURITY_DESCRIPTOR descriptor{};
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                            &descriptor, nullptr))
    throw std::runtime_error("Cannot secure pipe");
  SECURITY_ATTRIBUTES sa{sizeof(sa), descriptor, FALSE};
  Handle pipe(CreateNamedPipeW(
      (L"\\\\.\\pipe\\" + wide(name)).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536,
      0, &sa));
  LocalFree(descriptor);
  if (pipe.h == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot create local pipe");
  Service service;
  if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
    throw std::runtime_error("Pipe connection failed");
  std::string pending;
  std::array<char, 8192> chunk;
  DWORD got{};
  while (ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr) && got) {
    pending.append(chunk.data(), got);
    if (pending.size() > 65536)
      throw std::runtime_error("IPC request too large");
    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      pending.erase(0, pos + 1);
      json response;
      try {
        response = {{"ok", true}, {"result", service.call(json::parse(line))}};
      } catch (const std::exception &e) {
        response = {{"ok", false}, {"error", e.what()}};
      }
      auto text = response.dump() + "\n";
      DWORD sent = 0;
      size_t at = 0;
      while (at < text.size()) {
        if (!WriteFile(pipe, text.data() + at, static_cast<DWORD>(text.size() - at), &sent,
                       nullptr) ||
            !sent)
          return;
        at += sent;
      }
    }
  }
}
static void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
static json selftest() {
  Ring r(7);
  std::vector<float> x{0, 1, 2, 3, 4}, out(7);
  require(r.put(x), "ring first write");
  require(!r.put(x), "ring bounded");
  require(r.take({out.data(), 3}) == 3 && out[2] == 2, "ring first read");
  require(r.put(x), "ring wrapping write");
  require(r.take(out) == 7 && out[0] == 3 && out[6] == 4, "ring wrapping read");
  Ring concurrent(2048);
  std::atomic<bool> valid{true};
  std::thread producer([&] {
    for (int i = 0; i < 200000; ++i) {
      float value = static_cast<float>(i);
      while (!concurrent.put({&value, 1}))
        std::this_thread::yield();
    }
  });
  std::thread consumer([&] {
    for (int i = 0; i < 200000; ++i) {
      float value = 0;
      while (!concurrent.take({&value, 1}))
        std::this_thread::yield();
      if (value != static_cast<float>(i))
        valid = false;
    }
  });
  producer.join();
  consumer.join();
  require(valid, "lock-free ring order under contention");
  Ring recovery(10);
  require(recovery.put(x), "recovery write");
  recovery.discard();
  require(recovery.size() == 0, "consumer discard");
  require(recovery.put(x), "resume after discard");
  require(recovery.trim_to(2) == 3, "consumer playback trim");
  require(recovery.take(out) == 2 && out[0] == 3 && out[1] == 4, "trim retains newest samples");
  std::vector<float> tone(48000);
  for (size_t i = 0; i < tone.size(); ++i)
    tone[i] = static_cast<float>(.1 * std::sin(i * 2 * 3.14159265358979323846 * 440 / 48000));
  Resampler one(48000, 16000), parts(48000, 16000);
  auto expected = one.process(tone, true);
  std::vector<float> actual;
  for (size_t i = 0; i < tone.size(); i += 337) {
    auto y = parts.process({tone.data() + i, std::min<size_t>(337, tone.size() - i)});
    actual.insert(actual.end(), y.begin(), y.end());
  }
  auto tail = parts.process({}, true);
  actual.insert(actual.end(), tail.begin(), tail.end());
  require(actual.size() == 16000 && expected.size() == actual.size(), "resampler output length");
  double max_error = 0;
  for (size_t i = 0; i < actual.size(); ++i)
    max_error = std::max(max_error, double(std::abs(actual[i] - expected[i])));
  require(max_error < 1e-5, "resampler continuity");
  bool rejected = false;
  try {
    validate_reference(std::vector<float>(48000));
  } catch (...) {
    rejected = true;
  }
  require(rejected, "silent enrollment rejection");
  rejected = false;
  try {
    auto invalid = defaults();
    invalid["threads"] = 0;
    validate(invalid);
  } catch (...) {
    rejected = true;
  }
  require(rejected, "invalid threads rejected");
  return {{"native_core", "passed"},
          {"concurrent_samples", 200000},
          {"resampler_max_error", max_error}};
}
int wmain(int argc, wchar_t **argv) {
  try {
    if (argc < 2)
      throw std::runtime_error("Commands: serve, devices, hardware, setup, "
                               "isolate, calibrate, live, selftest");
    std::string command = utf8(argv[1]);
    std::map<std::string, std::string> args;
    for (int i = 2; i < argc; ++i) {
      std::string key = utf8(argv[i]);
      if (i + 1 >= argc)
        throw std::runtime_error("Missing argument value");
      args[key] = utf8(argv[++i]);
    }
    auto arg = [&](const char *key, const std::string &fallback = "") {
      auto it = args.find(key);
      return it == args.end() ? fallback : it->second;
    };
    json result;
    if (command == "serve") {
      serve(arg("--pipe"));
      return 0;
    }
    if (command == "selftest") {
      result = selftest();
      if (args.contains("--reference") && args.contains("--input")) {
        auto reference = audio16(wide(arg("--reference"))), audio = audio16(wide(arg("--input")));
        Model model(defaults(), reference);
        auto run = [&]() {
          std::vector<float> y;
          for (size_t i = 0; i < audio.size(); i += 1280) {
            auto part = model.push({audio.data() + i, std::min<size_t>(1280, audio.size() - i)});
            y.insert(y.end(), part.begin(), part.end());
          }
          auto tail = model.flush();
          y.insert(y.end(), tail.begin(), tail.end());
          require(y.size() >= audio.size(), "model sample count");
          y.resize(audio.size());
          return y;
        };
        auto first = run();
        model.reset();
        auto second = run();
        double difference = 0;
        for (size_t i = 0; i < first.size(); ++i)
          difference = std::max(difference, double(std::abs(first[i] - second[i])));
        require(difference == 0, "recurrent reset determinism");
        result["state_reset_max_error"] = difference;
        result["model_output_samples"] = first.size();
      }
    } else if (command == "devices")
      result = device_json();
    else if (command == "hardware")
      result = hardware();
    else if (command == "setup")
      result = setup_models();
    else if (command == "isolate") {
      auto s = defaults();
      s["threads"] = std::stoi(arg("--threads", "3"));
      s["chunk_ms"] = std::stoi(arg("--chunk-ms", "80"));
      s["model"] = arg("--model", "int8-full");
      s = validate(s);
      auto ref = audio16(wide(arg("--reference"))), audio = audio16(wide(arg("--input")));
      result = benchmark(s, ref, audio, wide(arg("--output")));
      if (!arg("--output").empty()) {
        auto path = fs::path(wide(arg("--output")));
        path.replace_extension(L".json");
        save_json(path, result);
      }
    } else if (command == "calibrate")
      result = calibrate(wide(arg("--reference")), std::stoi(arg("--seconds", "15")));
    else if (command == "live") {
      auto s = validate(read_json(root() / "settings.json"));
      if (args.contains("--threads"))
        s["threads"] = std::stoi(arg("--threads"));
      Engine e(s);
      e.start();
      int duration = std::stoi(arg("--seconds", "30")),
          inject = std::stoi(arg("--inject-recovery-at", "-1"));
      int inject_count = std::stoi(arg("--inject-count", "1")),
          inject_interval = std::stoi(arg("--inject-interval", "1"));
      if (inject_count < 1 || inject_interval < 1)
        throw std::runtime_error("Injection count and interval must be positive");
      std::ofstream report(wide(arg("--report", "native-live.jsonl")));
      if (!report)
        throw std::runtime_error("Cannot open report");
      for (int i = 0; i < duration; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (inject >= 0 && i >= inject && (i - inject) % inject_interval == 0 &&
            (i - inject) / inject_interval < inject_count)
          e.inject_stall(std::stoi(arg("--stall-ms", "0")));
        auto value = e.snapshot();
        value["elapsed_seconds"] = i + 1;
        report << value.dump() << '\n';
        report.flush();
        if (!value["running"].get<bool>())
          throw std::runtime_error(value["error"].get<std::string>());
      }
      result = e.snapshot();
      e.stop();
    } else
      throw std::runtime_error("Unknown command");
    std::cout << result.dump(2) << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
