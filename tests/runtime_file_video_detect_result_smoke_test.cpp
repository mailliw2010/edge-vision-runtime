#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"

namespace {

bool CommandOk(const std::string& command) {
  return std::system(command.c_str()) == 0;
}

std::vector<std::uint8_t> ReadCommandBytes(const std::string& command) {
  std::vector<std::uint8_t> bytes;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return bytes;
  }

  std::array<unsigned char, 8192> buffer{};
  while (true) {
    const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe);
    if (read > 0) {
      bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
    }
    if (read < buffer.size()) {
      if (std::feof(pipe) != 0) {
        break;
      }
      if (std::ferror(pipe) != 0) {
        bytes.clear();
        break;
      }
    }
  }

  if (pclose(pipe) != 0) {
    bytes.clear();
  }
  return bytes;
}

std::string EncodeDetectionResultJson(
    int frame_id,
    const std::string& source_uri,
    const evr::algorithm::yolov8_person_detection::Detection& detection) {
  std::ostringstream out;
  out << "{\"frame_id\":" << frame_id << ",\"source_uri\":\"" << source_uri
      << "\",\"class_name\":\"" << detection.class_name << "\",\"score\":" << detection.score
      << ",\"box\":{\"x\":" << detection.x << ",\"y\":" << detection.y
      << ",\"w\":" << detection.w << ",\"h\":" << detection.h << "}}";
  return out.str();
}

}  // namespace

int main() {
  if (!CommandOk("command -v ffmpeg >/dev/null 2>&1")) {
    std::cerr << "ffmpeg is required for runtime_file_video_detect_result_smoke_test\n";
    return 77;
  }

  const std::filesystem::path video_path =
      std::filesystem::temp_directory_path() / "evr_runtime_file_video_detect_result_smoke.mp4";
  std::filesystem::remove(video_path);

  const int frame_width = 64;
  const int frame_height = 64;
  const int frame_count = 3;
  const std::string source_uri = "file://" + video_path.string();

  const std::string generate_command =
      "ffmpeg -hide_banner -loglevel error -y -f lavfi -i "
      "testsrc=size=64x64:rate=3:duration=1 -frames:v 3 -c:v mpeg4 -q:v 5 " +
      video_path.string();
  assert(CommandOk(generate_command));
  assert(std::filesystem::exists(video_path));

  const std::string decode_command =
      "ffmpeg -hide_banner -loglevel error -i " + video_path.string() +
      " -frames:v 3 -f rawvideo -pix_fmt rgba -";
  const auto raw_video = ReadCommandBytes(decode_command);
  const std::size_t frame_bytes =
      static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height) * 4U;
  assert(raw_video.size() == frame_bytes * static_cast<std::size_t>(frame_count));

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  algorithm_config.backend = "synthetic";
  algorithm_config.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  algorithm_config.input_width = 640;
  algorithm_config.input_height = 640;
  algorithm_config.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  std::string error;
  assert(detector.LoadModel(&error));

  std::vector<std::string> encoded_results;
  for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
    const auto first = raw_video.begin() + static_cast<std::ptrdiff_t>(frame_bytes * frame_id);
    const auto last = first + static_cast<std::ptrdiff_t>(frame_bytes);
    const std::vector<std::uint8_t> rgba_frame(first, last);

    const auto detections = detector.Detect(rgba_frame, frame_width, frame_height, &error);
    assert(!detections.empty());
    assert(detections.front().class_name == "person");
    encoded_results.push_back(EncodeDetectionResultJson(frame_id, source_uri, detections.front()));
  }

  assert(encoded_results.size() == static_cast<std::size_t>(frame_count));
  assert(encoded_results.front().find("\"frame_id\":0") != std::string::npos);
  assert(encoded_results.front().find("\"source_uri\":\"file://") != std::string::npos);
  assert(encoded_results.front().find("\"class_name\":\"person\"") != std::string::npos);

  std::filesystem::remove(video_path);
  return 0;
}
