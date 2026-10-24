#pragma once
#include "common.hpp"
#include <onnxruntime_cxx_api.h>
#include <soxr.h>
namespace vl {
inline Ort::Env &ort_environment() {
  static bool initialized = []() {
    auto lib = LoadLibraryW((executable_dir() / L"onnxruntime.dll").c_str());
    if (!lib)
      throw std::runtime_error("onnxruntime.dll is missing beside the worker");
    auto get = reinterpret_cast<const OrtApiBase *(ORT_API_CALL *)()>(
        GetProcAddress(lib, "OrtGetApiBase"));
    if (!get)
      throw std::runtime_error("Invalid ONNX Runtime library");
    const auto *api = get()->GetApi(ORT_API_VERSION);
    if (!api)
      throw std::runtime_error("ONNX Runtime API version mismatch");
    Ort::InitApi(api);
    return true;
  }();
  (void)initialized;
  static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "VoiceLockNative");
  return env;
}
class Resampler {
  soxr_t handle{};

public:
  Resampler(int in, int out) {
    soxr_error_t error{};
    auto q = soxr_quality_spec(SOXR_LQ, 0);
    auto runtime = soxr_runtime_spec(1);
    handle = soxr_create(in, out, 1, &error, nullptr, &q, &runtime);
    if (error || !handle)
      throw std::runtime_error(error ? error : "Cannot create resampler");
  }
  ~Resampler() { soxr_delete(handle); }
  Resampler(const Resampler &) = delete;
  void clear() {
    auto e = soxr_clear(handle);
    if (e)
      throw std::runtime_error(e);
  }
  double delay() const { return soxr_delay(handle); }
  std::vector<float> process(std::span<const float> x, bool final = false) {
    std::vector<float> result;
    std::array<float, 8192> scratch;
    size_t at = 0;
    bool flush = final && x.empty();
    do {
      size_t used = 0, produced = 0;
      float dummy = 0;
      const float *input = flush ? nullptr : (x.empty() ? &dummy : x.data() + at);
      auto e = soxr_process(handle, input, x.size() - at, &used, scratch.data(), scratch.size(),
                            &produced);
      if (e)
        throw std::runtime_error(e);
      at += used;
      result.insert(result.end(), scratch.begin(), scratch.begin() + produced);
      if (flush && produced == 0)
        break;
      if (at == x.size() && !flush) {
        if (final)
          flush = true;
        else
          break;
      }
      if (!used && !produced && !flush && at < x.size())
        throw std::runtime_error("Resampler made no progress");
    } while (at < x.size() || flush);
    return result;
  }
};
inline std::vector<float> audio16(const fs::path &p) {
  auto w = read_wav(p);
  if (w.rate == 16000)
    return w.samples;
  Resampler r(w.rate, 16000);
  return r.process(w.samples, true);
}

// Port of VoxLock 1.1.1 Stream (Apache-2.0). The ONNX model includes STFT;
// this class performs the upstream reflection/overlap-add and recurrent carry.
class Model {
  Ort::Session enroll{nullptr}, chunk{nullptr};
  Ort::MemoryInfo memory{nullptr};
  std::vector<Ort::Value> embedding;
  std::vector<int64_t> mag_shape, ctx_shape;
  std::vector<Ort::AllocatedStringPtr> output_names;
  std::vector<const char *> outputs;
  std::vector<float> h = std::vector<float>(6 * 32 * 256), c = std::vector<float>(6 * 32 * 256);
  std::vector<float> buffer, ola = std::vector<float>(512), env = std::vector<float>(512);
  std::array<float, 512> win{};
  bool initialized = false;
  size_t dropped = 0;
  std::vector<float> run() {
    std::vector<float> out;
    while (buffer.size() >= 512) {
      size_t k = std::min<size_t>(100, 1 + (buffer.size() - 512) / 128);
      int64_t wav_shape[] = {1, static_cast<int64_t>((k - 1) * 128 + 512)},
              state_shape[] = {6, 32, 256};
      std::array<Ort::Value, 5> values = {
          Ort::Value::CreateTensor<float>(memory, buffer.data(), static_cast<size_t>(wav_shape[1]),
                                          wav_shape, 2),
          Ort::Value::CreateTensor<float>(
              memory, embedding[0].GetTensorMutableData<float>(),
              embedding[0].GetTensorTypeAndShapeInfo().GetElementCount(), mag_shape.data(),
              mag_shape.size()),
          Ort::Value::CreateTensor<float>(
              memory, embedding[1].GetTensorMutableData<float>(),
              embedding[1].GetTensorTypeAndShapeInfo().GetElementCount(), ctx_shape.data(),
              ctx_shape.size()),
          Ort::Value::CreateTensor<float>(memory, h.data(), h.size(), state_shape, 3),
          Ort::Value::CreateTensor<float>(memory, c.data(), c.size(), state_shape, 3)};
      const char *names[] = {"wav", "enroll_mag", "context", "h", "c"};
      // Output order is inspected from the actual model, not guessed names.
      auto result = chunk.Run(Ort::RunOptions{nullptr}, names, values.data(), 5, outputs.data(),
                              outputs.size());
      if (result.size() != 3 ||
          result[0].GetTensorTypeAndShapeInfo().GetElementCount() != k * 512 ||
          result[1].GetTensorTypeAndShapeInfo().GetElementCount() != h.size() ||
          result[2].GetTensorTypeAndShapeInfo().GetElementCount() != c.size())
        throw std::runtime_error("Unexpected VoxLock tensor dimensions");
      std::memcpy(h.data(), result[1].GetTensorData<float>(), h.size() * 4);
      std::memcpy(c.data(), result[2].GetTensorData<float>(), c.size() * 4);
      size_t done = k * 128;
      ola.resize(512 + done, 0);
      env.resize(512 + done, 0);
      const float *fr = result[0].GetTensorData<float>();
      for (size_t j = 0; j < k; ++j)
        for (size_t i = 0; i < 512; ++i) {
          ola[j * 128 + i] += fr[j * 512 + i];
          env[j * 128 + i] += win[i];
        }
      size_t cut = std::min(dropped, done);
      dropped -= cut;
      for (size_t i = cut; i < done; ++i) {
        float value = -.5f * ola[i] / (env[i] > 1e-11f ? env[i] : 1.f);
        if (!std::isfinite(value))
          throw std::runtime_error("Model returned non-finite audio");
        out.push_back(value);
      }
      std::move(ola.begin() + done, ola.end(), ola.begin());
      ola.resize(512);
      std::move(env.begin() + done, env.end(), env.begin());
      env.resize(512);
      buffer.erase(buffer.begin(), buffer.begin() + done);
    }
    return out;
  }

public:
  Model(const json &s, std::span<const float> reference) {
    auto &environment = ort_environment();
    validate_reference(reference);
    auto path = root() / "models" / "onnx";
    std::string filename =
        s.at("model") == "int8-full" ? "chunk_int8_full.onnx" : "chunk_int8.onnx";
    verify_model(path / "enroll.onnx");
    verify_model(path / filename);
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(s.at("threads").get<int>());
    options.SetInterOpNumThreads(1);
    options.SetExecutionMode(ORT_SEQUENTIAL);
    options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    options.AddConfigEntry("session.inter_op.allow_spinning", "0");
    // CPU is the default and only appended provider.
    enroll = Ort::Session(environment, (path / "enroll.onnx").c_str(), options);
    chunk = Ort::Session(environment, (path / filename).c_str(), options);
    memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    int64_t shape[] = {1, static_cast<int64_t>(reference.size())};
    auto input = Ort::Value::CreateTensor<float>(memory, const_cast<float *>(reference.data()),
                                                 reference.size(), shape, 2);
    const char *in[] = {"enroll"};
    Ort::AllocatorWithDefaultOptions a;
    auto out0 = enroll.GetOutputNameAllocated(0, a), out1 = enroll.GetOutputNameAllocated(1, a);
    const char *out[] = {out0.get(), out1.get()};
    embedding = enroll.Run(Ort::RunOptions{nullptr}, in, &input, 1, out, 2);
    mag_shape = embedding[0].GetTensorTypeAndShapeInfo().GetShape();
    ctx_shape = embedding[1].GetTensorTypeAndShapeInfo().GetShape();
    for (size_t i = 0; i < chunk.GetOutputCount(); ++i) {
      output_names.push_back(chunk.GetOutputNameAllocated(i, a));
      outputs.push_back(output_names.back().get());
    }
    for (size_t i = 0; i < 512; ++i) {
      float w = static_cast<float>(.5 - .5 * std::cos(2 * 3.14159265358979323846 * i / 512));
      win[i] = w * w;
    }
    buffer.reserve(16000);
    ola.reserve(14000);
    env.reserve(14000);
  }
  void reset() {
    std::fill(h.begin(), h.end(), 0.f);
    std::fill(c.begin(), c.end(), 0.f);
    buffer.clear();
    ola.assign(512, 0);
    env.assign(512, 0);
    initialized = false;
    dropped = 0;
  }
  std::vector<float> push(std::span<const float> x) {
    buffer.insert(buffer.end(), x.begin(), x.end());
    if (!initialized) {
      if (buffer.size() <= 256)
        return {};
      std::array<float, 256> prefix{};
      for (size_t i = 0; i < 256; ++i)
        prefix[i] = buffer[256 - i];
      buffer.insert(buffer.begin(), prefix.begin(), prefix.end());
      initialized = true;
      dropped = 256;
    }
    return run();
  }
  std::vector<float> flush() {
    if (!initialized)
      return {};
    if (buffer.size() < 257)
      throw std::runtime_error("Insufficient flush history");
    size_t n = buffer.size();
    std::array<float, 256> tail{};
    for (size_t i = 0; i < 256; ++i)
      tail[i] = buffer[n - 2 - i];
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    auto y = run();
    for (size_t i = 0; i < 512; ++i)
      y.push_back(-.5f * ola[i] / (env[i] > 1e-11f ? env[i] : 1.f));
    initialized = false;
    return y;
  }
};

// Single producer / single consumer only. Neither thread modifies the other's
// cursor; acquire/release publishes data with no mutex, GIL or allocation.
class Ring {
  std::vector<float> data;
  alignas(64) std::atomic<uint64_t> read{0};
  alignas(64) std::atomic<uint64_t> write{0};

public:
  explicit Ring(size_t capacity) : data(capacity) {
    if (!capacity)
      throw std::runtime_error("Empty ring");
  }
  size_t size() const {
    auto r = read.load(std::memory_order_acquire), w = write.load(std::memory_order_acquire);
    return static_cast<size_t>(w - r);
  }
  bool put(std::span<const float> x) {
    auto w = write.load(std::memory_order_relaxed), r = read.load(std::memory_order_acquire);
    if (x.size() > data.size() - (w - r))
      return false;
    size_t at = static_cast<size_t>(w % data.size()), first = std::min(x.size(), data.size() - at);
    std::copy_n(x.data(), first, data.data() + at);
    std::copy_n(x.data() + first, x.size() - first, data.data());
    write.store(w + x.size(), std::memory_order_release);
    return true;
  }
  size_t take(std::span<float> out) {
    auto r = read.load(std::memory_order_relaxed), w = write.load(std::memory_order_acquire);
    size_t n = std::min(out.size(), static_cast<size_t>(w - r)),
           at = static_cast<size_t>(r % data.size()), first = std::min(n, data.size() - at);
    std::copy_n(data.data() + at, first, out.data());
    std::copy_n(data.data(), n - first, out.data() + first);
    read.store(r + n, std::memory_order_release);
    return n;
  }
  void discard() { read.store(write.load(std::memory_order_acquire), std::memory_order_release); }
  size_t trim_to(size_t keep) {
    auto r = read.load(std::memory_order_relaxed), w = write.load(std::memory_order_acquire);
    size_t count = (w - r) > keep ? static_cast<size_t>(w - r) - keep : 0;
    read.store(r + count, std::memory_order_release);
    return count;
  }
};
} // namespace vl
