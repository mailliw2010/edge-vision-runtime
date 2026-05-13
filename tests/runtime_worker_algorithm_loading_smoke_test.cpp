#include <cassert>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"

int main() {
  evr::algorithm::yolov8_person_detection::AlgorithmConfig cfg;
  cfg.backend = "synthetic";
  cfg.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  cfg.input_width = 640;
  cfg.input_height = 640;
  cfg.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(cfg);
  std::string error;
  assert(detector.LoadModel(&error));

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(640) * 640 * 4U, 127U);
  const auto tensor = detector.Preprocess(rgba, 640, 640, &error);
  assert(!tensor.empty());

  const auto detections = detector.Infer(tensor, 640, 640, &error);
  assert(!detections.empty());
  assert(detections.front().class_name == "person");

  const std::vector<float> raw_output{0.0F, 0.90F, 10.0F, 20.0F, 30.0F, 40.0F};
  const auto post = detector.Postprocess(raw_output, 640, 640, &error);
  assert(!post.empty());
  assert(post.front().class_name == "person");
  return 0;
}
