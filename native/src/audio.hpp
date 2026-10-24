#pragma once
#include "model.hpp"
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
namespace vl {
struct AudioScheduling {
  HANDLE task{};
  AudioScheduling() {
    DWORD index = 0;
    task = AvSetMmThreadCharacteristicsW(L"Audio", &index);
  }
  ~AudioScheduling() {
    if (task)
      AvRevertMmThreadCharacteristics(task);
  }
};
inline ComPtr<IMMDeviceEnumerator> enumerator() {
  ComPtr<IMMDeviceEnumerator> e;
  check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)),
        "Audio enumeration");
  return e;
}
struct Device {
  std::string id, name;
  int channels{}, rate{};
  bool input{};
};
inline std::vector<Device> devices() {
  Com com;
  auto e = enumerator();
  std::vector<Device> list;
  for (auto flow : {eCapture, eRender}) {
    ComPtr<IMMDeviceCollection> collection;
    check(e->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection), "Endpoint list");
    UINT n = 0;
    collection->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
      ComPtr<IMMDevice> d;
      check(collection->Item(i, &d), "Endpoint");
      LPWSTR id{};
      check(d->GetId(&id), "Endpoint ID");
      std::string key = utf8(id);
      CoTaskMemFree(id);
      ComPtr<IPropertyStore> props;
      check(d->OpenPropertyStore(STGM_READ, &props), "Endpoint properties");
      PROPVARIANT v;
      PropVariantInit(&v);
      check(props->GetValue(PKEY_Device_FriendlyName, &v), "Endpoint name");
      std::string name = v.vt == VT_LPWSTR ? utf8(v.pwszVal) : key;
      PropVariantClear(&v);
      ComPtr<IAudioClient> client;
      if (FAILED(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                             reinterpret_cast<void **>(client.GetAddressOf()))))
        continue;
      WAVEFORMATEX *f{};
      if (FAILED(client->GetMixFormat(&f)))
        continue;
      list.push_back(
          {key, name, f->nChannels, static_cast<int>(f->nSamplesPerSec), flow == eCapture});
      CoTaskMemFree(f);
    }
  }
  return list;
}
inline json device_json() {
  json out = json::array();
  for (auto &d : devices())
    out.push_back({{"id", d.id},
                   {"name", d.name},
                   {"input", d.input ? d.channels : 0},
                   {"output", d.input ? 0 : d.channels},
                   {"rate", d.rate}});
  return out;
}
inline Device resolve(const std::string &name, bool input) {
  std::vector<Device> found;
  for (auto &d : devices())
    if (d.input == input && (d.name == name || d.id == name))
      found.push_back(d);
  if (found.size() != 1)
    throw std::runtime_error("Select a unique connected " +
                             std::string(input ? "microphone: " : "playback endpoint: ") + name);
  return found[0];
}
class Wasapi {
public:
  ComPtr<IAudioClient> client;
  ComPtr<IAudioCaptureClient> capture;
  ComPtr<IAudioRenderClient> render;
  Handle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  UINT32 frames{};
  double latency{};
  int channels;
  Wasapi(const Device &d, int rate) : channels(d.input ? 1 : 2) {
    auto e = enumerator();
    ComPtr<IMMDevice> dev;
    check(e->GetDevice(wide(d.id).c_str(), &dev), "Open endpoint");
    check(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void **>(client.GetAddressOf())),
          "Activate audio");
    WAVEFORMATEX f{};
    f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    f.nChannels = static_cast<WORD>(channels);
    f.nSamplesPerSec = rate;
    f.wBitsPerSample = 32;
    f.nBlockAlign = static_cast<WORD>(channels * 4);
    f.nAvgBytesPerSec = rate * f.nBlockAlign;
    check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                             0, 0, &f, nullptr),
          "WASAPI shared format");
    check(client->SetEventHandle(event), "Audio event");
    check(client->GetBufferSize(&frames), "Audio buffer size");
    REFERENCE_TIME delay{};
    check(client->GetStreamLatency(&delay), "Audio latency");
    latency = delay / 10000.;
    if (d.input)
      check(client->GetService(IID_PPV_ARGS(&capture)), "Capture service");
    else
      check(client->GetService(IID_PPV_ARGS(&render)), "Render service");
  }
  ~Wasapi() {
    if (client)
      client->Stop();
  }
  void start() { check(client->Start(), "Start WASAPI stream"); }
};
class Engine {
  json settings;
  Device input, output;
  int input_rate, output_rate, chunk_size;
  Ring capture_ring, output_ring;
  std::unique_ptr<Model> model;
  std::unique_ptr<Resampler> to_model, to_output;
  Handle stop_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)},
      wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  std::thread capture_thread, render_thread, infer_thread;
  std::atomic<bool> alive{false}, muted{true}, recover_requested{false};
  std::atomic<int> injected_delay_ms{0};
  std::atomic<uint64_t> playback_trims{0};
  std::atomic<unsigned> render_epoch{0}, render_ack{0};
  std::atomic<uint64_t> overruns{0}, underruns{0}, device_errors{0}, recoveries{0},
      startup_blocks{0};
  std::atomic<double> input_level{0}, output_level{0}, capture_latency{0}, render_latency{0},
      resample_delay{0};
  std::mutex stats_mutex;
  std::vector<double> times;
  std::string error, warning;
  double last_cpu = cpu_seconds(), last_status = seconds();
  static double level(std::span<const float> x) {
    double energy = 0;
    for (auto v : x)
      energy += v * v;
    return x.empty() ? 0 : std::sqrt(energy / x.size());
  }
  void fail(const std::string &message) {
    muted = true;
    alive = false;
    {
      std::lock_guard lock(stats_mutex);
      error = message;
    }
    SetEvent(stop_event);
    SetEvent(wake);
  }
  void capture_loop(std::promise<void> ready) {
    bool announced = false;
    try {
      Com com;
      AudioScheduling scheduling;
      Wasapi stream(input, input_rate);
      std::vector<float> silence(stream.frames, 0);
      capture_latency = stream.latency;
      stream.start();
      ready.set_value();
      announced = true;
      bool received = false;
      HANDLE events[] = {stop_event.h, stream.event.h};
      while (alive) {
        auto wait = WaitForMultipleObjects(2, events, FALSE, 2000);
        if (wait == WAIT_OBJECT_0)
          break;
        if (wait != WAIT_OBJECT_0 + 1)
          throw std::runtime_error("Microphone stopped responding; reconnect and restart");
        UINT32 pending = 0;
        check(stream.capture->GetNextPacketSize(&pending), "Capture packet");
        while (pending) {
          BYTE *data{};
          UINT32 n{};
          DWORD flags{};
          check(stream.capture->GetBuffer(&data, &n, &flags, nullptr, nullptr), "Capture buffer");
          bool discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) && received;
          received = true;
          bool put = false;
          if (n <= silence.size()) {
            auto src = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? silence.data()
                                                            : reinterpret_cast<float *>(data);
            put = capture_ring.put({src, n});
          }
          check(stream.capture->ReleaseBuffer(n), "Release capture");
          if (!put) {
            ++overruns;
            muted = true;
            recover_requested = true;
          }
          if (discontinuity) {
            ++device_errors;
            muted = true;
            recover_requested = true;
          }
          SetEvent(wake);
          check(stream.capture->GetNextPacketSize(&pending), "Next capture packet");
        }
      }
    } catch (...) {
      if (!announced)
        ready.set_exception(std::current_exception());
      try {
        throw;
      } catch (const std::exception &e) {
        fail(e.what());
      }
    }
  }
  void render_loop(std::promise<void> ready) {
    bool announced = false;
    try {
      Com com;
      AudioScheduling scheduling;
      Wasapi stream(output, output_rate);
      std::vector<float> mono(stream.frames);
      render_latency = stream.latency;
      stream.start();
      ready.set_value();
      announced = true;
      bool primed = false, ever = false;
      unsigned epoch = 0;
      HANDLE events[] = {stop_event.h, stream.event.h};
      while (alive) {
        auto wait = WaitForMultipleObjects(2, events, FALSE, 2000);
        if (wait == WAIT_OBJECT_0)
          break;
        if (wait != WAIT_OBJECT_0 + 1)
          throw std::runtime_error("Playback endpoint stopped responding; reconnect and restart");
        auto requested = render_epoch.load();
        if (requested != epoch) {
          output_ring.discard();
          epoch = requested;
          primed = false;
          render_ack = epoch;
          SetEvent(wake);
        }
        // The render consumer alone drops stale, already-isolated audio.
        // Allow the chosen cushion plus one inference burst and resampler/
        // device packet jitter. A fixed 160 ms threshold can otherwise trim
        // healthy 80 ms-cushion bursts and create avoidable underruns.
        double cushion=settings.at("output_buffer_ms").get<double>();
        double chunk_ms=settings.at("chunk_ms").get<double>();
        if (output_ring.size() > static_cast<size_t>(output_rate * (cushion+chunk_ms+40)/1000.)) {
          output_ring.trim_to(static_cast<size_t>(output_rate * (cushion+chunk_ms/2)/1000.));
          ++playback_trims;
        }
        UINT32 padding{};
        check(stream.client->GetCurrentPadding(&padding), "Playback padding");
        UINT32 available = stream.frames - padding;
        if (!available)
          continue;
        BYTE *raw{};
        check(stream.render->GetBuffer(available, &raw), "Playback buffer");
        auto out = reinterpret_cast<float *>(raw);
        std::fill_n(out, available * stream.channels, 0.f);
        if (!muted) {
          if (!primed && output_ring.size() >= static_cast<size_t>(output_rate * settings.at("output_buffer_ms").get<double>()/1000.))
            primed = true;
          if (primed) {
            size_t n = output_ring.take({mono.data(), available});
            for (size_t i = 0; i < n; ++i)
              for (int ch = 0; ch < stream.channels; ++ch)
                out[i * stream.channels + ch] = mono[i];
            ever = true;
            if (n < available) {
              ++underruns;
              primed = false;
            }
          } else if (!ever)
            ++startup_blocks;
        }
        if (muted)
          std::fill_n(out, available * stream.channels, 0.f);
        check(stream.render->ReleaseBuffer(available, 0), "Release playback");
      }
    } catch (...) {
      if (!announced)
        ready.set_exception(std::current_exception());
      try {
        throw;
      } catch (const std::exception &e) {
        fail(e.what());
      }
    }
  }
  void infer_loop() {
    std::vector<float> scratch(static_cast<size_t>(input_rate * .02)), accumulator;
    accumulator.reserve(4096);
    std::vector<double> recent_recoveries;
    try {
      while (alive) {
        WaitForSingleObject(wake, 50);
        if (!alive)
          break;
        if (int delay = injected_delay_ms.exchange(0); delay > 0)
          std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        if (recover_requested.exchange(false) ||
            capture_ring.size() > static_cast<size_t>(input_rate * .16)) {
          muted = true;
          double now = seconds();
          recent_recoveries.erase(std::remove_if(recent_recoveries.begin(), recent_recoveries.end(),
                                                 [&](double t) { return now - t > 10; }),
                                  recent_recoveries.end());
          recent_recoveries.push_back(now);
          // Load spikes are recoverable, including repeated spikes. Never latch
          // the engine off merely because a recovery count was exceeded.
          // Keep the same enrollment and bounded queues; device/model errors
          // still go through fail() rather than passing through raw audio.
          bool sustained = recent_recoveries.size() > 5;
          unsigned epoch = ++render_epoch;
          capture_ring.discard();
          model->reset();
          to_model->clear();
          to_output->clear();
          accumulator.clear();
          while (alive && render_ack.load() != epoch)
            WaitForSingleObject(wake, 20);
          if (!alive)
            break;
          ++recoveries;
          {
            std::lock_guard lock(stats_mutex);
            warning = sustained
                          ? "Repeated processing stalls: automatic recovery active. "
                            "Audio may have gaps; no restart needed."
                          : "Recovered automatically from an audio stall. "
                            "No restart needed; no raw microphone sent.";
          }
          continue;
        }
        while (alive && capture_ring.size() && !recover_requested) {
          size_t n = capture_ring.take(scratch);
          input_level = level({scratch.data(), n});
          auto x = to_model->process({scratch.data(), n});
          accumulator.insert(accumulator.end(), x.begin(), x.end());
          while (accumulator.size() >= static_cast<size_t>(chunk_size) && alive) {
            double start = seconds();
            auto isolated = model->push({accumulator.data(), static_cast<size_t>(chunk_size)});
            double elapsed = (seconds() - start) * 1000;
            {
              std::lock_guard lock(stats_mutex);
              if (times.size() == 1000)
                times.erase(times.begin());
              times.push_back(elapsed);
            }
            accumulator.erase(accumulator.begin(), accumulator.begin() + chunk_size);
            if (recover_requested)
              break;
            auto y = to_output->process(isolated);
            double gain = settings.at("gain");
            for (float &v : y) {
              if (!std::isfinite(v))
                throw std::runtime_error("Invalid model audio");
              v = std::clamp(static_cast<float>(v * gain), -.98f, .98f);
            }
            output_level = level(y);
            resample_delay = to_model->delay() / 16. + to_output->delay() / output_rate * 1000;
            if (!output_ring.put(y)) {
              ++overruns;
              muted = true;
              unsigned epoch = ++render_epoch;
              while (alive && render_ack.load() != epoch)
                WaitForSingleObject(wake, 20);
              if (!alive)
                break;
              ++playback_trims;
              if (!output_ring.put(y))
                throw std::runtime_error("Playback queue recovery failed");
            }
            if (!y.empty())
              muted = false;
          }
          if (capture_ring.size() > static_cast<size_t>(input_rate * .16))
            break;
        }
      }
    } catch (const std::exception &e) {
      fail(e.what());
    }
  }

public:
  Engine(json s)
      : settings(validate(s)), input(resolve(settings.at("input_device"), true)),
        output(resolve(settings.at("output_device"), false)), input_rate(settings.at("input_rate")),
        output_rate(settings.at("output_rate")),
        chunk_size(settings.at("chunk_ms").get<int>() * 16),
        capture_ring(static_cast<size_t>(input_rate * .24)),
        output_ring(static_cast<size_t>(output_rate * .24)) {}
  ~Engine() { stop(); }
  void start() {
    auto reference = audio16(root() / "enrollment.wav");
    model = std::make_unique<Model>(settings, reference);
    to_model = std::make_unique<Resampler>(input_rate, 16000);
    to_output = std::make_unique<Resampler>(16000, output_rate);
    times.reserve(1000);
    alive = true;
    std::promise<void> cap, out;
    auto cf = cap.get_future(), of = out.get_future();
    capture_thread = std::thread(&Engine::capture_loop, this, std::move(cap));
    render_thread = std::thread(&Engine::render_loop, this, std::move(out));
    try {
      cf.get();
      of.get();
      if (!alive)
        throw std::runtime_error("Audio device initialization failed");
      infer_thread = std::thread(&Engine::infer_loop, this);
    } catch (...) {
      stop();
      throw;
    }
  }
  void stop() {
    muted = true;
    alive = false;
    SetEvent(stop_event);
    SetEvent(wake);
    if (capture_thread.joinable())
      capture_thread.join();
    if (render_thread.joinable())
      render_thread.join();
    if (infer_thread.joinable())
      infer_thread.join();
    model.reset();
    to_model.reset();
    to_output.reset();
  }
  json snapshot() {
    std::lock_guard lock(stats_mutex);
    double p95 = percentile(times, .95), now = seconds(), cpu = cpu_seconds(),
           usage = (cpu - last_cpu) / std::max(.001, now - last_status) /
                   std::max(1u, std::thread::hardware_concurrency()) * 100;
    last_cpu = cpu;
    last_status = now;
    double queues = capture_ring.size() / double(input_rate) * 1000 +
                    output_ring.size() / double(output_rate) * 1000;
    bool economy = p95 / settings.at("chunk_ms").get<double>() > .9 || queues > 120;
    MEMORYSTATUSEX memory{sizeof(memory)};
    GlobalMemoryStatusEx(&memory);
    return {{"backend", "C++20 native"},
            {"running", alive.load()},
            {"muted", muted.load()},
            {"error", error},
            {"warning", warning},
            {"model", settings.at("model")},
            {"threads", settings.at("threads")},
            {"chunk_ms", settings.at("chunk_ms")},
            {"input_rate", input_rate},
            {"output_rate", output_rate},
            {"input_level", input_level.load()},
            {"output_level", muted ? 0 : output_level.load()},
            {"inference_mean_ms",
             times.empty() ? 0 : std::accumulate(times.begin(), times.end(), 0.) / times.size()},
            {"inference_p95_ms", p95},
            {"rtf_p95", p95 / settings.at("chunk_ms").get<double>()},
            {"queue_ms", queues},
            {"output_buffer_ms",settings.at("output_buffer_ms")},
            {"capture_queue_ms", capture_ring.size() / double(input_rate) * 1000},
            {"output_queue_ms", output_ring.size() / double(output_rate) * 1000},
            {"playback_trims", playback_trims.load()},
            {"estimated_latency_ms", settings.at("chunk_ms").get<double>() + 24 + p95 + queues +
                                         capture_latency + render_latency + resample_delay},
            {"cpu_percent", usage},
            {"ram_mb", rss_mb()},
            {"available_ram_mb", memory.ullAvailPhys / 1048576.},
            {"overruns", overruns.load()},
            {"underruns", underruns.load()},
            {"device_errors", device_errors.load()},
            {"recoveries", recoveries.load()},
            {"startup_silence_blocks", startup_blocks.load()},
            {"economy", economy}};
  }
  void inject_stall(int delay = 0) {
    if (delay > 0)
      injected_delay_ms = std::min(delay, 1000);
    else {
      recover_requested = true;
      muted = true;
    }
    SetEvent(wake);
  }
};
inline json enroll(const json &s, bool consent) {
  if (!consent)
    throw std::runtime_error("Local voice reference storage requires consent");
  auto d = resolve(s.at("input_device"), true);
  Com com;
  int rate = s.at("input_rate");
  Wasapi stream(d, rate);
  std::vector<float> audio;
  audio.reserve(rate * 3);
  stream.start();
  double deadline = seconds() + 6;
  while (audio.size() < static_cast<size_t>(rate * 3)) {
    if (seconds() > deadline || WaitForSingleObject(stream.event, 2000) != WAIT_OBJECT_0)
      throw std::runtime_error("Microphone did not deliver enrollment audio");
    UINT32 pending = 0;
    check(stream.capture->GetNextPacketSize(&pending), "Enrollment packet");
    while (pending) {
      BYTE *data{};
      UINT32 n{};
      DWORD flags{};
      check(stream.capture->GetBuffer(&data, &n, &flags, nullptr, nullptr), "Enrollment buffer");
      size_t count = std::min<size_t>(n, rate * 3 - audio.size());
      if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        audio.insert(audio.end(), count, 0);
      else
        audio.insert(audio.end(), reinterpret_cast<float *>(data),
                     reinterpret_cast<float *>(data) + count);
      check(stream.capture->ReleaseBuffer(n), "Enrollment release");
      check(stream.capture->GetNextPacketSize(&pending), "Enrollment next packet");
    }
  }
  Resampler resampler(rate, 16000);
  auto reference = resampler.process(audio, true);
  validate_reference(reference);
  fs::create_directories(root());
  auto tmp = root() / "enrollment.native.tmp.wav";
  write_wav(tmp, reference);
  if (!MoveFileExW(tmp.c_str(), (root() / "enrollment.wav").c_str(), MOVEFILE_REPLACE_EXISTING))
    throw std::runtime_error("Cannot save enrollment");
  return {{"seconds", reference.size() / 16000.}};
}
} // namespace vl
