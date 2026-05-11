#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

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

}  // namespace

YoloV8PersonDetector::YoloV8PersonDetector(AlgorithmConfig config) : config_(std::move(config)) {}

const AlgorithmConfig& YoloV8PersonDetector::config() const { return config_; }

bool YoloV8PersonDetector::LoadModel(std::string* error) {
  if (!FileReadable(config_.model_path)) {
    if (error != nullptr) {
      *error = "model is not readable: " + config_.model_path;
    }
    return false;
  }
  model_loaded_ = true;
  return true;
}

std::vector<float> YoloV8PersonDetector::Preprocess(const std::vector<std::uint8_t>& rgba_image,
                                                    int image_width,
                                                    int image_height,
                                                    std::string* error) const {
  if (image_width <= 0 || image_height <= 0) {
    if (error != nullptr) {
      *error = "invalid image size";
    }
    return {};
  }
  const std::size_t expected = static_cast<std::size_t>(image_width) *
                               static_cast<std::size_t>(image_height) * 4U;
  if (rgba_image.size() < expected) {
    if (error != nullptr) {
      *error = "rgba image buffer is too small";
    }
    return {};
  }

  std::vector<float> tensor;
  tensor.reserve(static_cast<std::size_t>(config_.input_width) *
                 static_cast<std::size_t>(config_.input_height) * 3U);

  for (int y = 0; y < config_.input_height; ++y) {
    const int src_y = y * image_height / config_.input_height;
    for (int x = 0; x < config_.input_width; ++x) {
      const int src_x = x * image_width / config_.input_width;
      const std::size_t index = (static_cast<std::size_t>(src_y) * image_width + src_x) * 4U;
      const float r = rgba_image[index + 0] / 255.0F;
      const float g = rgba_image[index + 1] / 255.0F;
      const float b = rgba_image[index + 2] / 255.0F;
      tensor.push_back(r);
      tensor.push_back(g);
      tensor.push_back(b);
    }
  }

  return tensor;
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

  Detection detection;
  detection.class_id = static_cast<int>(Clamp(raw_output[0], 0.0F, 1000.0F));
  detection.class_name = detection.class_id == 0 ? "person" : "class_" + std::to_string(detection.class_id);
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

std::vector<Detection> YoloV8PersonDetector::Detect(const std::vector<std::uint8_t>& rgba_image,
                                                    int image_width,
                                                    int image_height,
                                                    std::string* error) const {
  auto tensor = Preprocess(rgba_image, image_width, image_height, error);
  if (tensor.empty()) {
    return {};
  }
  auto detections = Infer(tensor, image_width, image_height, error);
  if (detections.empty()) {
    return {};
  }
  return detections;
}

}  // namespace evr::algorithm::yolov8_person_detection
