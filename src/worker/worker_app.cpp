#include "evr/runtime/worker/worker_app.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/session/session_state.h"

namespace evr::runtime::worker {
namespace {

std::string ResolveLocalPath(const std::string& uri_or_path) {
  constexpr const char* kFileScheme = "file://";
  if (uri_or_path.rfind(kFileScheme, 0) == 0) {
    return uri_or_path.substr(7);
  }
  return uri_or_path;
}

bool PathReadable(const std::string& uri_or_path) {
  if (uri_or_path.empty()) {
    return false;
  }
  const std::string path = ResolveLocalPath(uri_or_path);
  return std::filesystem::exists(path);
}

bool LoadYoloConfig(const std::string& config_uri,
                    evr::algorithm::yolov8_person_detection::AlgorithmConfig* config,
                    std::string* error) {
  if (config == nullptr) {
    if (error != nullptr) {
      *error = "algorithm config output is null";
    }
    return false;
  }

  const std::string path = ResolveLocalPath(config_uri);
  std::ifstream input(path);
  if (!input) {
    if (error != nullptr) {
      *error = "failed to open algorithm config: " + path;
    }
    return false;
  }

  std::string line;
  std::string current_section;
  while (std::getline(input, line)) {
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
      continue;
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    const std::string trimmed = line.substr(first, last - first + 1);
    if (trimmed.back() == ':' && trimmed.find(':') == trimmed.size() - 1) {
      current_section = trimmed.substr(0, trimmed.size() - 1);
      continue;
    }
    const auto sep = trimmed.find(':');
    if (sep == std::string::npos) {
      continue;
    }
    const std::string key = trimmed.substr(0, sep);
    std::string value = trimmed.substr(sep + 1);
    const auto val_first = value.find_first_not_of(" \t");
    if (val_first != std::string::npos) {
      value = value.substr(val_first);
    }
    const auto val_last = value.find_last_not_of(" \t\r\n");
    if (val_last != std::string::npos) {
      value = value.substr(0, val_last + 1);
    }
    if (current_section == "algorithm") {
      if (key == "name") config->name = value;
      if (key == "backend") config->backend = value;
      if (key == "model_path") config->model_path = value;
      if (key == "input_width") config->input_width = std::stoi(value);
      if (key == "input_height") config->input_height = std::stoi(value);
      if (key == "confidence_threshold") config->confidence_threshold = std::stof(value);
      if (key == "nms_threshold") config->nms_threshold = std::stof(value);
    }
  }
  return true;
}

void PrintSnapshot(const session::Snapshot& snapshot) {
  std::cout << snapshot.kind << "[" << snapshot.session_id << "] "
            << session::ToString(snapshot.state) << " - " << snapshot.detail << '\n';
}

}  // namespace

WorkerApp::WorkerApp(WorkerAppConfig config) : config_(std::move(config)) {}

int WorkerApp::Run() {
  if (!PathReadable(config_.algorithm.package_uri)) {
    std::cerr << "algorithm package is not readable: " << config_.algorithm.package_uri << std::endl;
    return 1;
  }
  if (!PathReadable(config_.algorithm.runtime_config_uri)) {
    std::cerr << "algorithm runtime config is not readable: " << config_.algorithm.runtime_config_uri
              << std::endl;
    return 1;
  }

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  if (!LoadYoloConfig(config_.algorithm.runtime_config_uri, &algorithm_config, nullptr)) {
    std::cerr << "failed to load algorithm config" << std::endl;
    return 1;
  }
  if (!config_.algorithm.model_path.empty()) {
    algorithm_config.model_path = config_.algorithm.model_path;
  } else {
    algorithm_config.model_path = ResolveLocalPath(config_.worker.engine_path);
  }

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  std::string algorithm_error;
  if (!detector.LoadModel(&algorithm_error)) {
    std::cerr << algorithm_error << std::endl;
    return 1;
  }

  source::SourceSession source_session;
  WorkerSession worker_session;

  if (!source_session.Configure(config_.source)) {
    std::cerr << "failed to configure source session" << std::endl;
    return 1;
  }

  if (!worker_session.Configure(config_.worker)) {
    std::cerr << "failed to configure worker session" << std::endl;
    return 1;
  }

  if (!source_session.Start()) {
    std::cerr << "failed to start source session" << std::endl;
    return 1;
  }

  if (!worker_session.Start()) {
    std::cerr << "failed to start worker session" << std::endl;
    return 1;
  }

  std::vector<source::FrameBuffer> frames;
  if (config_.decode_source_frames) {
    frames = source_session.CaptureFramesFromSource(config_.source_frame_width,
                                                    config_.source_frame_height,
                                                    config_.source_frame_count,
                                                    &algorithm_error);
  } else {
    frames.push_back(source_session.MakeSyntheticFrame(config_.source_frame_width,
                                                       config_.source_frame_height));
  }
  if (frames.empty()) {
    std::cerr << "no source frames: " << algorithm_error << std::endl;
    worker_session.Stop();
    source_session.Stop();
    return 1;
  }

  PrintSnapshot(source_session.GetSnapshot());
  PrintSnapshot(worker_session.GetSnapshot());
  for (std::size_t frame_id = 0; frame_id < frames.size(); ++frame_id) {
    const auto& frame = frames[frame_id];
    const auto detections = detector.Detect(frame.rgba, frame.width, frame.height, &algorithm_error);
    if (detections.empty()) {
      std::cerr << "no detections for frame " << frame_id << ": " << algorithm_error << std::endl;
      worker_session.Stop();
      source_session.Stop();
      return 1;
    }
    for (const auto& detection : detections) {
      std::cout << "detection[frame=" << frame_id << ", class=" << detection.class_name
                << ", score=" << detection.score << ", x=" << detection.x
                << ", y=" << detection.y << ", w=" << detection.w << ", h=" << detection.h
                << "]\n";
    }
  }

  worker_session.Stop();
  source_session.Stop();
  return 0;
}

}  // namespace evr::runtime::worker
