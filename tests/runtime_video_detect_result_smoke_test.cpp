#include <cassert>
#include <sstream>
#include <string>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/source/source_session.h"
#include "evr/runtime/worker/worker_session.h"

namespace {

std::string EncodeDetectionResultJson(
    int frame_id,
    const std::string& output_topic,
    const evr::algorithm::yolov8_person_detection::Detection& detection) {
  std::ostringstream out;
  out << "{\"frame_id\":" << frame_id << ",\"output_topic\":\"" << output_topic
      << "\",\"class_name\":\"" << detection.class_name << "\",\"score\":" << detection.score
      << ",\"box\":{\"x\":" << detection.x << ",\"y\":" << detection.y
      << ",\"w\":" << detection.w << ",\"h\":" << detection.h << "}}";
  return out.str();
}

}  // namespace

int main() {
  using evr::runtime::session::State;

  evr::runtime::source::SourceSession source_session;
  evr::runtime::source::SourceSessionConfig source_config;
  source_config.session_id = "video-source-smoke";
  source_config.source_uri = "synthetic://rgba-gray-frames";
  source_config.upstream_kind = "synthetic";
  source_config.transport_protocol = "in-process";
  source_config.buffer_transport = "host-memory";
  source_config.decode_mode = "synthetic-rgba";
  source_config.pixel_format = "rgba";
  assert(source_session.Configure(source_config));
  assert(source_session.Start());
  assert(source_session.state() == State::kRunning);

  evr::runtime::worker::WorkerSession worker_session;
  evr::runtime::worker::WorkerSessionConfig worker_config;
  worker_config.session_id = "video-worker-smoke";
  worker_config.source_session_id = source_config.session_id;
  worker_config.inference_backend = "onnxruntime-smoke";
  worker_config.engine_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  worker_config.algorithm_name = "yolov8-person-detection";
  worker_config.input_binding = "frames";
  worker_config.result_encoding = "json";
  worker_config.output_topic = "events.detection.smoke";
  assert(worker_session.Configure(worker_config));
  assert(worker_session.Start());
  assert(worker_session.state() == State::kRunning);

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  algorithm_config.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  algorithm_config.input_width = 640;
  algorithm_config.input_height = 640;
  algorithm_config.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  std::string error;
  assert(detector.LoadModel(&error));

  std::vector<std::string> encoded_results;
  for (int frame_id = 0; frame_id < 3; ++frame_id) {
    const auto frame = source_session.MakeSyntheticFrame(640, 640);
    const auto detections = detector.Detect(frame.rgba, frame.width, frame.height, &error);
    assert(!detections.empty());
    assert(detections.front().class_name == "person");
    assert(detections.front().score >= algorithm_config.confidence_threshold);

    encoded_results.push_back(
        EncodeDetectionResultJson(frame_id, worker_config.output_topic, detections.front()));
  }

  assert(encoded_results.size() == 3U);
  assert(encoded_results.front().find("\"frame_id\":0") != std::string::npos);
  assert(encoded_results.front().find("\"output_topic\":\"events.detection.smoke\"") !=
         std::string::npos);
  assert(encoded_results.front().find("\"class_name\":\"person\"") != std::string::npos);

  worker_session.Stop();
  source_session.Stop();
  assert(worker_session.state() == State::kStopped);
  assert(source_session.state() == State::kStopped);

  return 0;
}
