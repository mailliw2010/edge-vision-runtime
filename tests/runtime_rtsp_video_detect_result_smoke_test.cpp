#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
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

std::string RedactUri(const std::string& uri) {
  const auto scheme = uri.find("://");
  if (scheme == std::string::npos) {
    return uri;
  }
  const auto at = uri.find('@', scheme + 3);
  if (at == std::string::npos) {
    return uri;
  }
  return uri.substr(0, scheme + 3) + "***:***@" + uri.substr(at + 1);
}

std::string EncodeDetectionResultJson(
    int frame_id,
    const std::string& source_uri,
    const evr::algorithm::yolov8_person_detection::Detection& detection) {
  std::ostringstream out;
  out << "{\"frame_id\":" << frame_id << ",\"source_uri\":\"" << RedactUri(source_uri)
      << "\",\"class_name\":\"" << detection.class_name << "\",\"score\":" << detection.score
      << ",\"box\":{\"x\":" << detection.x << ",\"y\":" << detection.y
      << ",\"w\":" << detection.w << ",\"h\":" << detection.h << "}}";
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  const std::string uri_env_name = argc > 1 ? argv[1] : "EVR_TEST_RTSP_URI";
  const std::string source_label = argc > 2 ? argv[2] : "rtsp";
  const char* rtsp_uri_env = std::getenv(uri_env_name.c_str());
  if (rtsp_uri_env == nullptr || std::string(rtsp_uri_env).empty()) {
    std::cerr << "set " << uri_env_name << " to run " << source_label
              << " video detect result smoke test\n";
    return 77;
  }

  if (!CommandOk("command -v ffmpeg >/dev/null 2>&1")) {
    std::cerr << "ffmpeg is required for " << source_label
              << " video detect result smoke test\n";
    return 77;
  }

  const std::string rtsp_uri = rtsp_uri_env;
  const int frame_width = 640;
  const int frame_height = 360;
  const int frame_count = 3;

  const std::string decode_command =
      "timeout 25s ffmpeg -hide_banner -loglevel error -rtsp_transport tcp "
      "-stimeout 10000000 -i " +
      ShellQuote(rtsp_uri) +
      " -an -frames:v 3 -vf scale=640:360 -f rawvideo -pix_fmt rgba - "
      "2>/tmp/evr_runtime_rtsp_video_detect_result_ffmpeg.log";
  const auto raw_video = ReadCommandBytes(decode_command);
  const std::size_t frame_bytes =
      static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height) * 4U;
  if (raw_video.size() != frame_bytes * static_cast<std::size_t>(frame_count)) {
    std::cerr << "failed to decode RTSP frames from " << RedactUri(rtsp_uri)
              << "; expected " << frame_count << " RGBA frames, got " << raw_video.size()
              << " bytes\n";
    return 1;
  }

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
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
    encoded_results.push_back(EncodeDetectionResultJson(frame_id, rtsp_uri, detections.front()));
  }

  assert(encoded_results.size() == static_cast<std::size_t>(frame_count));
  assert(encoded_results.front().find("\"frame_id\":0") != std::string::npos);
  assert(encoded_results.front().find("\"source_uri\":\"rtsp://") != std::string::npos);
  assert(encoded_results.front().find("\"class_name\":\"person\"") != std::string::npos);

  return 0;
}
