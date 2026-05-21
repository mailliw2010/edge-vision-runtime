#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/source/source_session.h"

namespace {

std::string EncodeDetectionResultJson(
    int frame_id,
    const std::string& source_uri,
    const evr::algorithm::yolov8_person_detection::Detection& detection) {
  std::ostringstream out;
  out << "{\"frame_id\":" << frame_id << ",\"source_uri\":\"" << source_uri
      << "\",\"decode_backend\":\"gstreamer-nv12-host\",\"class_name\":\""
      << detection.class_name << "\",\"score\":" << detection.score << "}";
  return out.str();
}

}  // namespace

int main() {
  const int frame_width = 64;
  const int frame_height = 64;
  const int frame_count = 3;
  const std::string source_uri = "gst-testsrc://smpte";

  evr::runtime::source::SourceSession source_session;
  evr::runtime::source::SourceSessionConfig source_config;
  source_config.session_id = "gstreamer-nv12-host-source-smoke";
  source_config.source_uri = source_uri;
  source_config.upstream_kind = "gstreamer-testsrc";
  source_config.transport_protocol = "in-process";
  source_config.buffer_transport = "host-memory";
  source_config.decode_mode = "gstreamer-nv12-host";
  source_config.pixel_format = "nv12";
  source_config.decode_timeout_seconds = 10;
  assert(source_session.Configure(source_config));
  assert(source_session.Start());

  std::string error;
  const auto frames =
      source_session.CaptureFramesFromSource(frame_width, frame_height, frame_count, &error);
  if (frames.size() != static_cast<std::size_t>(frame_count)) {
    std::cerr << error << '\n';
    return 1;
  }

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  algorithm_config.backend = "synthetic";
  algorithm_config.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  algorithm_config.input_width = 640;
  algorithm_config.input_height = 640;
  algorithm_config.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  assert(detector.LoadModel(&error));

  std::vector<std::string> encoded_results;
  for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
    const auto& frame = frames[static_cast<std::size_t>(frame_id)];
    assert(frame.pixel_format == "nv12");
    const auto detections =
        detector.DetectImage(frame.bytes, frame.width, frame.height, frame.pixel_format, &error);
    assert(!detections.empty());
    assert(detections.front().class_name == "person");
    encoded_results.push_back(EncodeDetectionResultJson(frame_id, source_uri, detections.front()));
  }

  assert(encoded_results.size() == static_cast<std::size_t>(frame_count));
  assert(encoded_results.front().find("\"decode_backend\":\"gstreamer-nv12-host\"") !=
         std::string::npos);
  assert(encoded_results.front().find("\"class_name\":\"person\"") != std::string::npos);

  source_session.Stop();
  return 0;
}
