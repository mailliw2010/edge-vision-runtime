#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace evr::algorithm::yolov8_person_detection {

struct AlgorithmConfig {
  std::string name{"yolov8-person-detection"};
  std::string backend{"tensorrt"};
  std::string model_path;
  int input_width{640};
  int input_height{640};
  float confidence_threshold{0.25F};
  float nms_threshold{0.45F};
  std::vector<std::string> classes{"person"};
};

struct Detection {
  int class_id{0};
  std::string class_name{"person"};
  float score{0.0F};
  float x{0.0F};
  float y{0.0F};
  float w{0.0F};
  float h{0.0F};
};

class YoloV8PersonDetector {
 public:
  explicit YoloV8PersonDetector(AlgorithmConfig config);
  ~YoloV8PersonDetector();

  const AlgorithmConfig& config() const;
  bool LoadModel(std::string* error);

  std::vector<float> PreprocessImage(const std::vector<std::uint8_t>& image,
                                     int image_width,
                                     int image_height,
                                     const std::string& pixel_format,
                                     std::string* error) const;

  std::vector<float> Preprocess(const std::vector<std::uint8_t>& rgba_image,
                                int image_width,
                                int image_height,
                                std::string* error) const;

  std::vector<Detection> Infer(const std::vector<float>& input,
                               int image_width,
                               int image_height,
                               std::string* error) const;

  std::vector<Detection> Postprocess(const std::vector<float>& raw_output,
                                     int image_width,
                                     int image_height,
                                     std::string* error) const;

  std::vector<Detection> DetectImage(const std::vector<std::uint8_t>& image,
                                     int image_width,
                                     int image_height,
                                     const std::string& pixel_format,
                                     std::string* error) const;

  std::vector<Detection> Detect(const std::vector<std::uint8_t>& rgba_image,
                                int image_width,
                                int image_height,
                                std::string* error) const;

 private:
  struct RuntimeState;

  AlgorithmConfig config_;
  bool model_loaded_{false};
  std::unique_ptr<RuntimeState> runtime_state_;
};

}  // namespace evr::algorithm::yolov8_person_detection
