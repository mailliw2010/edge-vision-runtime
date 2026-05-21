#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/source/source_session.h"

namespace {

bool CommandOk(const std::string& command) {
  return std::system(command.c_str()) == 0;
}

struct StreamFixture {
  std::string source_session_id;
  std::filesystem::path video_path;
  std::string source_uri;
};

std::string EncodeDetectionResultJson(
    const std::string& source_session_id,
    int frame_id,
    const evr::algorithm::yolov8_person_detection::Detection& detection) {
  std::ostringstream out;
  out << "{\"source_session_id\":\"" << source_session_id << "\",\"frame_id\":" << frame_id
      << ",\"class_name\":\"" << detection.class_name << "\",\"score\":" << detection.score
      << ",\"box\":{\"x\":" << detection.x << ",\"y\":" << detection.y
      << ",\"w\":" << detection.w << ",\"h\":" << detection.h << "}}";
  return out.str();
}

StreamFixture MakeStreamFixture(const std::string& source_session_id,
                                const std::string& pattern,
                                int index) {
  StreamFixture fixture;
  fixture.source_session_id = source_session_id;
  fixture.video_path = std::filesystem::temp_directory_path() /
                       ("evr_runtime_multi_stream_" + std::to_string(index) + ".mp4");
  fixture.source_uri = "file://" + fixture.video_path.string();
  std::filesystem::remove(fixture.video_path);

  const std::string command =
      "ffmpeg -hide_banner -loglevel error -y -f lavfi -i " + pattern +
      " -frames:v 3 -c:v mpeg4 -q:v 5 " + fixture.video_path.string();
  assert(CommandOk(command));
  assert(std::filesystem::exists(fixture.video_path));
  return fixture;
}

}  // namespace

int main() {
  if (!CommandOk("command -v ffmpeg >/dev/null 2>&1")) {
    std::cerr << "ffmpeg is required for runtime_multi_stream_one_algorithm_smoke_test\n";
    return 77;
  }

  const int frame_width = 64;
  const int frame_height = 64;
  const int frame_count = 3;
  const std::vector<StreamFixture> streams = {
      MakeStreamFixture("source-camera-0", "testsrc=size=64x64:rate=3:duration=1", 0),
      MakeStreamFixture("source-camera-1", "smptebars=size=64x64:rate=3:duration=1", 1),
  };

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
  for (const auto& stream : streams) {
    evr::runtime::source::SourceSession source_session;
    evr::runtime::source::SourceSessionConfig source_config;
    source_config.session_id = stream.source_session_id;
    source_config.source_uri = stream.source_uri;
    source_config.upstream_kind = "file";
    source_config.transport_protocol = "file";
    source_config.buffer_transport = "host-memory";
    source_config.decode_mode = "ffmpeg";
    source_config.pixel_format = "rgba";
    source_config.decode_log_path =
        "/tmp/evr_runtime_multi_stream_" + stream.source_session_id + "_ffmpeg.log";
    assert(source_session.Configure(source_config));
    assert(source_session.Start());

    const auto frames =
        source_session.CaptureFramesFromSource(frame_width, frame_height, frame_count, &error);
    assert(frames.size() == static_cast<std::size_t>(frame_count));

    for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
      const auto& frame = frames[static_cast<std::size_t>(frame_id)];
      const auto detections =
          detector.DetectImage(frame.bytes, frame.width, frame.height, frame.pixel_format, &error);
      assert(!detections.empty());
      assert(detections.front().class_name == "person");
      encoded_results.push_back(
          EncodeDetectionResultJson(stream.source_session_id, frame_id, detections.front()));
    }

    source_session.Stop();
  }

  assert(encoded_results.size() == streams.size() * static_cast<std::size_t>(frame_count));
  assert(encoded_results.front().find("\"source_session_id\":\"source-camera-0\"") !=
         std::string::npos);
  assert(encoded_results.back().find("\"source_session_id\":\"source-camera-1\"") !=
         std::string::npos);
  assert(encoded_results.back().find("\"class_name\":\"person\"") != std::string::npos);

  for (const auto& stream : streams) {
    std::filesystem::remove(stream.video_path);
  }
  return 0;
}
