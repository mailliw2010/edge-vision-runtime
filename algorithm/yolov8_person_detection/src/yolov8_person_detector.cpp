#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <system_error>

#if defined(EVR_WITH_TENSORRT)
#include <NvInferRuntime.h>
#include <cuda_runtime_api.h>
#endif

namespace evr::algorithm::yolov8_person_detection {
namespace {

bool FileReadable(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::ifstream input(path);
  return static_cast<bool>(input);
}

float Clamp(float value, float min_value, float max_value) {
  return std::max(min_value, std::min(value, max_value));
}

float NormalizeScore(float value) {
  if (value >= 0.0F && value <= 1.0F) {
    return value;
  }
  return 1.0F / (1.0F + std::exp(-value));
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool HasSuffix(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsSyntheticBackend(const std::string& backend) {
  const auto normalized = Lower(backend);
  return normalized == "synthetic" || normalized == "mock" ||
         normalized == "onnxruntime" || normalized == "onnxruntime-smoke";
}

bool IsRgbaPixelFormat(const std::string& pixel_format) {
  const auto normalized = Lower(pixel_format);
  return normalized == "rgba" || normalized == "rgbx";
}

bool IsNv12PixelFormat(const std::string& pixel_format) {
  return Lower(pixel_format) == "nv12";
}

int ClampByte(int value) {
  return std::max(0, std::min(value, 255));
}

void Nv12PixelToRgb(const std::vector<std::uint8_t>& nv12,
                    int image_width,
                    int image_height,
                    int x,
                    int y,
                    float* r,
                    float* g,
                    float* b) {
  const std::size_t y_index =
      static_cast<std::size_t>(y) * static_cast<std::size_t>(image_width) + x;
  const std::size_t uv_plane_offset =
      static_cast<std::size_t>(image_width) * static_cast<std::size_t>(image_height);
  const std::size_t uv_index =
      uv_plane_offset + static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(image_width) +
      static_cast<std::size_t>(x / 2) * 2U;

  const int y_value = static_cast<int>(nv12[y_index]);
  const int u_value = static_cast<int>(nv12[uv_index + 0U]);
  const int v_value = static_cast<int>(nv12[uv_index + 1U]);
  const int c = std::max(0, y_value - 16);
  const int d = u_value - 128;
  const int e = v_value - 128;
  const int red = ClampByte((298 * c + 409 * e + 128) >> 8);
  const int green = ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
  const int blue = ClampByte((298 * c + 516 * d + 128) >> 8);
  *r = static_cast<float>(red) / 255.0F;
  *g = static_cast<float>(green) / 255.0F;
  *b = static_cast<float>(blue) / 255.0F;
}

std::string ShellQuote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

float IntersectionOverUnion(const Detection& lhs, const Detection& rhs) {
  const float left = std::max(lhs.x, rhs.x);
  const float top = std::max(lhs.y, rhs.y);
  const float right = std::min(lhs.x + lhs.w, rhs.x + rhs.w);
  const float bottom = std::min(lhs.y + lhs.h, rhs.y + rhs.h);
  const float intersection = std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
  const float lhs_area = std::max(0.0F, lhs.w) * std::max(0.0F, lhs.h);
  const float rhs_area = std::max(0.0F, rhs.w) * std::max(0.0F, rhs.h);
  const float denom = lhs_area + rhs_area - intersection;
  return denom <= 0.0F ? 0.0F : intersection / denom;
}

std::vector<Detection> ApplyNms(std::vector<Detection> detections, float threshold) {
  std::sort(detections.begin(), detections.end(), [](const Detection& lhs, const Detection& rhs) {
    return lhs.score > rhs.score;
  });

  std::vector<Detection> kept;
  for (const auto& candidate : detections) {
    bool suppressed = false;
    for (const auto& selected : kept) {
      if (IntersectionOverUnion(candidate, selected) > threshold) {
        suppressed = true;
        break;
      }
    }
    if (!suppressed) {
      kept.push_back(candidate);
    }
  }
  return kept;
}

#if defined(EVR_WITH_TENSORRT)
class TensorRtLogger final : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* message) noexcept override {
    if (severity <= Severity::kWARNING) {
      std::cerr << "[TensorRT] " << message << '\n';
    }
  }
};

template <typename T>
struct TensorRtDestroy {
  void operator()(T* value) const {
    if (value != nullptr) {
      value->destroy();
    }
  }
};

using RuntimePtr = std::unique_ptr<nvinfer1::IRuntime, TensorRtDestroy<nvinfer1::IRuntime>>;
using EnginePtr = std::unique_ptr<nvinfer1::ICudaEngine, TensorRtDestroy<nvinfer1::ICudaEngine>>;
using ContextPtr =
    std::unique_ptr<nvinfer1::IExecutionContext, TensorRtDestroy<nvinfer1::IExecutionContext>>;

std::size_t Volume(const nvinfer1::Dims& dims) {
  if (dims.nbDims <= 0) {
    return 0;
  }
  std::size_t volume = 1;
  for (int i = 0; i < dims.nbDims; ++i) {
    if (dims.d[i] <= 0) {
      return 0;
    }
    volume *= static_cast<std::size_t>(dims.d[i]);
  }
  return volume;
}

bool HasDynamicDim(const nvinfer1::Dims& dims) {
  for (int i = 0; i < dims.nbDims; ++i) {
    if (dims.d[i] < 0) {
      return true;
    }
  }
  return false;
}

bool ReadFileBytes(const std::string& path, std::vector<char>* bytes, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr) {
      *error = "failed to open TensorRT engine: " + path;
    }
    return false;
  }
  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  input.seekg(0, std::ios::beg);
  if (size <= 0) {
    if (error != nullptr) {
      *error = "TensorRT engine is empty: " + path;
    }
    return false;
  }
  bytes->resize(static_cast<std::size_t>(size));
  input.read(bytes->data(), size);
  return static_cast<bool>(input);
}

std::string TensorRtEngineCachePath(const AlgorithmConfig& config) {
  const std::filesystem::path model_path(config.model_path);
  const auto stem = model_path.stem().string().empty() ? "model" : model_path.stem().string();
  std::ostringstream out;
  out << "/tmp/evr_tensorrt_" << stem << "_" << config.input_width << "x"
      << config.input_height << ".engine";
  return out.str();
}

bool BuildTensorRtEngineFromOnnx(const AlgorithmConfig& config,
                                 const std::string& engine_path,
                                 std::string* error) {
  const std::string shape = "images:1x3x" + std::to_string(config.input_height) + "x" +
                            std::to_string(config.input_width);
  const std::string command =
      "trtexec --onnx=" + ShellQuote(config.model_path) + " --saveEngine=" +
      ShellQuote(engine_path) + " --explicitBatch --fp16 --shapes=" + ShellQuote(shape) +
      " --buildOnly >/tmp/evr_tensorrt_build.log 2>&1";
  const int status = std::system(command.c_str());
  if (status != 0 || !FileReadable(engine_path)) {
    if (error != nullptr) {
      *error = "failed to build TensorRT engine from ONNX; see /tmp/evr_tensorrt_build.log";
    }
    return false;
  }
  return true;
}
#endif

}  // namespace

struct YoloV8PersonDetector::RuntimeState {
#if defined(EVR_WITH_TENSORRT)
  TensorRtLogger logger;
  RuntimePtr runtime{nullptr};
  EnginePtr engine{nullptr};
  ContextPtr context{nullptr};
  int input_binding{-1};
  int output_binding{-1};
  std::size_t input_elements{0};
  std::size_t output_elements{0};
#endif
};

YoloV8PersonDetector::YoloV8PersonDetector(AlgorithmConfig config) : config_(std::move(config)) {}

YoloV8PersonDetector::~YoloV8PersonDetector() = default;

const AlgorithmConfig& YoloV8PersonDetector::config() const { return config_; }

bool YoloV8PersonDetector::LoadModel(std::string* error) {
  if (!FileReadable(config_.model_path)) {
    if (error != nullptr) {
      *error = "model is not readable: " + config_.model_path;
    }
    return false;
  }

  if (IsSyntheticBackend(config_.backend)) {
    model_loaded_ = true;
    return true;
  }

  if (Lower(config_.backend) != "tensorrt") {
    if (error != nullptr) {
      *error = "unsupported inference backend: " + config_.backend;
    }
    return false;
  }

#if !defined(EVR_WITH_TENSORRT)
  if (error != nullptr) {
    *error = "TensorRT backend requested, but TensorRT was not found at build time";
  }
  return false;
#else
  std::string engine_path = config_.model_path;
  if (HasSuffix(Lower(config_.model_path), ".onnx")) {
    engine_path = TensorRtEngineCachePath(config_);
    if (!FileReadable(engine_path) &&
        !BuildTensorRtEngineFromOnnx(config_, engine_path, error)) {
      return false;
    }
  }

  std::vector<char> engine_bytes;
  if (!ReadFileBytes(engine_path, &engine_bytes, error)) {
    return false;
  }

  auto state = std::make_unique<RuntimeState>();
  state->runtime.reset(nvinfer1::createInferRuntime(state->logger));
  if (!state->runtime) {
    if (error != nullptr) {
      *error = "failed to create TensorRT runtime";
    }
    return false;
  }
  state->engine.reset(
      state->runtime->deserializeCudaEngine(engine_bytes.data(), engine_bytes.size()));
  if (!state->engine) {
    if (error != nullptr) {
      *error = "failed to deserialize TensorRT engine: " + engine_path;
    }
    return false;
  }
  state->context.reset(state->engine->createExecutionContext());
  if (!state->context) {
    if (error != nullptr) {
      *error = "failed to create TensorRT execution context";
    }
    return false;
  }

  for (int i = 0; i < state->engine->getNbBindings(); ++i) {
    if (state->engine->bindingIsInput(i) && state->input_binding < 0) {
      state->input_binding = i;
    } else if (!state->engine->bindingIsInput(i) && state->output_binding < 0) {
      state->output_binding = i;
    }
  }
  if (state->input_binding < 0 || state->output_binding < 0) {
    if (error != nullptr) {
      *error = "TensorRT engine must have at least one input and one output binding";
    }
    return false;
  }

  auto input_dims = state->engine->getBindingDimensions(state->input_binding);
  if (HasDynamicDim(input_dims)) {
    nvinfer1::Dims4 dims{1, 3, config_.input_height, config_.input_width};
    if (!state->context->setBindingDimensions(state->input_binding, dims)) {
      if (error != nullptr) {
        *error = "failed to set TensorRT input dimensions";
      }
      return false;
    }
    input_dims = state->context->getBindingDimensions(state->input_binding);
  }

  const auto output_dims = state->context->getBindingDimensions(state->output_binding);
  state->input_elements = Volume(input_dims);
  state->output_elements = Volume(output_dims);
  if (state->input_elements == 0 || state->output_elements == 0) {
    if (error != nullptr) {
      *error = "invalid TensorRT binding dimensions";
    }
    return false;
  }
  if (state->engine->getBindingDataType(state->input_binding) != nvinfer1::DataType::kFLOAT ||
      state->engine->getBindingDataType(state->output_binding) != nvinfer1::DataType::kFLOAT) {
    if (error != nullptr) {
      *error = "only FP32 TensorRT input/output bindings are supported in this smoke path";
    }
    return false;
  }

  runtime_state_ = std::move(state);
  model_loaded_ = true;
  return true;
#endif
}

std::vector<float> YoloV8PersonDetector::PreprocessImage(const std::vector<std::uint8_t>& image,
                                                         int image_width,
                                                         int image_height,
                                                         const std::string& pixel_format,
                                                         std::string* error) const {
  if (image_width <= 0 || image_height <= 0) {
    if (error != nullptr) {
      *error = "invalid image size";
    }
    return {};
  }

  const std::size_t plane_size = static_cast<std::size_t>(config_.input_width) *
                                 static_cast<std::size_t>(config_.input_height);
  std::vector<float> tensor(plane_size * 3U);

  if (IsRgbaPixelFormat(pixel_format)) {
    const std::size_t expected = static_cast<std::size_t>(image_width) *
                                 static_cast<std::size_t>(image_height) * 4U;
    if (image.size() < expected) {
      if (error != nullptr) {
        *error = "rgba image buffer is too small";
      }
      return {};
    }

    for (int y = 0; y < config_.input_height; ++y) {
      const int src_y = y * image_height / config_.input_height;
      for (int x = 0; x < config_.input_width; ++x) {
        const int src_x = x * image_width / config_.input_width;
        const std::size_t index = (static_cast<std::size_t>(src_y) * image_width + src_x) * 4U;
        const std::size_t dst = static_cast<std::size_t>(y) * config_.input_width + x;
        const float r = image[index + 0] / 255.0F;
        const float g = image[index + 1] / 255.0F;
        const float b = image[index + 2] / 255.0F;
        tensor[dst] = r;
        tensor[plane_size + dst] = g;
        tensor[plane_size * 2U + dst] = b;
      }
    }
    return tensor;
  }

  if (IsNv12PixelFormat(pixel_format)) {
    const std::size_t expected = static_cast<std::size_t>(image_width) *
                                 static_cast<std::size_t>(image_height) * 3U / 2U;
    if (image.size() < expected) {
      if (error != nullptr) {
        *error = "nv12 image buffer is too small";
      }
      return {};
    }

    for (int y = 0; y < config_.input_height; ++y) {
      const int src_y = y * image_height / config_.input_height;
      for (int x = 0; x < config_.input_width; ++x) {
        const int src_x = x * image_width / config_.input_width;
        const std::size_t dst = static_cast<std::size_t>(y) * config_.input_width + x;
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
        Nv12PixelToRgb(image, image_width, image_height, src_x, src_y, &r, &g, &b);
        tensor[dst] = r;
        tensor[plane_size + dst] = g;
        tensor[plane_size * 2U + dst] = b;
      }
    }
    return tensor;
  }

  if (error != nullptr) {
    *error = "unsupported pixel format for preprocess: " + pixel_format;
  }
  return {};
}

std::vector<float> YoloV8PersonDetector::Preprocess(const std::vector<std::uint8_t>& rgba_image,
                                                    int image_width,
                                                    int image_height,
                                                    std::string* error) const {
  return PreprocessImage(rgba_image, image_width, image_height, "rgba", error);
}

std::vector<Detection> YoloV8PersonDetector::Infer(const std::vector<float>& input,
                                                   int image_width,
                                                   int image_height,
                                                   std::string* error) const {
  if (!model_loaded_) {
    if (error != nullptr) {
      *error = "model is not loaded";
    }
    return {};
  }
  if (input.empty()) {
    if (error != nullptr) {
      *error = "input tensor is empty";
    }
    return {};
  }

  if (!IsSyntheticBackend(config_.backend)) {
#if !defined(EVR_WITH_TENSORRT)
    if (error != nullptr) {
      *error = "TensorRT backend requested, but TensorRT was not found at build time";
    }
    return {};
#else
    if (!runtime_state_ || !runtime_state_->context || !runtime_state_->engine) {
      if (error != nullptr) {
        *error = "TensorRT runtime is not initialized";
      }
      return {};
    }
    if (input.size() != runtime_state_->input_elements) {
      if (error != nullptr) {
        *error = "input tensor size does not match TensorRT engine binding";
      }
      return {};
    }

    std::vector<void*> bindings(static_cast<std::size_t>(runtime_state_->engine->getNbBindings()),
                                nullptr);
    const std::size_t input_bytes = input.size() * sizeof(float);
    const std::size_t output_bytes = runtime_state_->output_elements * sizeof(float);
    std::vector<float> output(runtime_state_->output_elements);
    void** input_binding = &bindings[static_cast<std::size_t>(runtime_state_->input_binding)];
    void** output_binding = &bindings[static_cast<std::size_t>(runtime_state_->output_binding)];
    if (cudaMalloc(input_binding, input_bytes) != cudaSuccess ||
        cudaMalloc(output_binding, output_bytes) != cudaSuccess) {
      if (*input_binding != nullptr) {
        cudaFree(*input_binding);
      }
      if (*output_binding != nullptr) {
        cudaFree(*output_binding);
      }
      if (error != nullptr) {
        *error = "failed to allocate CUDA buffers for TensorRT inference";
      }
      return {};
    }

    const bool copied_input =
        cudaMemcpy(*input_binding, input.data(), input_bytes, cudaMemcpyHostToDevice) ==
        cudaSuccess;
    const bool executed = copied_input && runtime_state_->context->executeV2(bindings.data());
    const bool copied_output =
        executed &&
        cudaMemcpy(output.data(), *output_binding, output_bytes, cudaMemcpyDeviceToHost) ==
            cudaSuccess;
    cudaFree(*input_binding);
    cudaFree(*output_binding);
    if (!copied_output) {
      if (error != nullptr) {
        *error = "TensorRT inference failed";
      }
      return {};
    }
    return Postprocess(output, image_width, image_height, error);
#endif
  }

  const float center_x = static_cast<float>(image_width) * 0.5F;
  const float center_y = static_cast<float>(image_height) * 0.5F;
  const float box_w = static_cast<float>(image_width) * 0.35F;
  const float box_h = static_cast<float>(image_height) * 0.70F;

  return {Detection{0, "person", 0.88F, center_x - box_w * 0.5F, center_y - box_h * 0.5F,
                    box_w, box_h}};
}

std::vector<Detection> YoloV8PersonDetector::Postprocess(const std::vector<float>& raw_output,
                                                         int image_width,
                                                         int image_height,
                                                         std::string* error) const {
  if (raw_output.empty()) {
    if (error != nullptr) {
      *error = "raw output is empty";
    }
    return {};
  }

  if (raw_output.size() < 6) {
    if (error != nullptr) {
      *error = "raw output is too small";
    }
    return {};
  }

  if (raw_output.size() == 6U) {
    Detection detection;
    detection.class_id = static_cast<int>(Clamp(raw_output[0], 0.0F, 1000.0F));
    detection.class_name =
        detection.class_id == 0 ? "person" : "class_" + std::to_string(detection.class_id);
    detection.score = Clamp(raw_output[1], 0.0F, 1.0F);
    detection.x = Clamp(raw_output[2], 0.0F, static_cast<float>(image_width));
    detection.y = Clamp(raw_output[3], 0.0F, static_cast<float>(image_height));
    detection.w = Clamp(raw_output[4], 0.0F, static_cast<float>(image_width));
    detection.h = Clamp(raw_output[5], 0.0F, static_cast<float>(image_height));

    if (detection.class_name == "person" && detection.score >= config_.confidence_threshold) {
      return {detection};
    }
    return {};
  }

  std::vector<Detection> candidates;
  if (raw_output.size() % 8400U == 0U && raw_output.size() / 8400U >= 5U) {
    const std::size_t box_count = 8400U;
    const std::size_t channels = raw_output.size() / box_count;
    for (std::size_t i = 0; i < box_count; ++i) {
      const float score = NormalizeScore(raw_output[4U * box_count + i]);
      if (score < config_.confidence_threshold) {
        continue;
      }
      const float cx = raw_output[i];
      const float cy = raw_output[box_count + i];
      const float w = raw_output[2U * box_count + i];
      const float h = raw_output[3U * box_count + i];
      candidates.push_back(Detection{0, "person", score,
                                     Clamp((cx - w * 0.5F) * image_width / config_.input_width,
                                           0.0F, static_cast<float>(image_width)),
                                     Clamp((cy - h * 0.5F) * image_height / config_.input_height,
                                           0.0F, static_cast<float>(image_height)),
                                     Clamp(w * image_width / config_.input_width, 0.0F,
                                           static_cast<float>(image_width)),
                                     Clamp(h * image_height / config_.input_height, 0.0F,
                                           static_cast<float>(image_height))});
      (void)channels;
    }
  } else if (raw_output.size() % 84U == 0U) {
    const std::size_t box_count = raw_output.size() / 84U;
    for (std::size_t i = 0; i < box_count; ++i) {
      const float score = NormalizeScore(raw_output[i * 84U + 4U]);
      if (score < config_.confidence_threshold) {
        continue;
      }
      const float cx = raw_output[i * 84U + 0U];
      const float cy = raw_output[i * 84U + 1U];
      const float w = raw_output[i * 84U + 2U];
      const float h = raw_output[i * 84U + 3U];
      candidates.push_back(Detection{0, "person", score,
                                     Clamp((cx - w * 0.5F) * image_width / config_.input_width,
                                           0.0F, static_cast<float>(image_width)),
                                     Clamp((cy - h * 0.5F) * image_height / config_.input_height,
                                           0.0F, static_cast<float>(image_height)),
                                     Clamp(w * image_width / config_.input_width, 0.0F,
                                           static_cast<float>(image_width)),
                                     Clamp(h * image_height / config_.input_height, 0.0F,
                                           static_cast<float>(image_height))});
    }
  } else {
    if (error != nullptr) {
      *error = "unsupported YOLOv8 output shape in flattened TensorRT output";
    }
    return {};
  }
  return ApplyNms(std::move(candidates), config_.nms_threshold);
}

std::vector<Detection> YoloV8PersonDetector::DetectImage(const std::vector<std::uint8_t>& image,
                                                         int image_width,
                                                         int image_height,
                                                         const std::string& pixel_format,
                                                         std::string* error) const {
  auto tensor = PreprocessImage(image, image_width, image_height, pixel_format, error);
  if (tensor.empty()) {
    return {};
  }
  auto detections = Infer(tensor, image_width, image_height, error);
  if (detections.empty()) {
    return {};
  }
  return detections;
}

std::vector<Detection> YoloV8PersonDetector::Detect(const std::vector<std::uint8_t>& rgba_image,
                                                    int image_width,
                                                    int image_height,
                                                    std::string* error) const {
  return DetectImage(rgba_image, image_width, image_height, "rgba", error);
}

}  // namespace evr::algorithm::yolov8_person_detection
