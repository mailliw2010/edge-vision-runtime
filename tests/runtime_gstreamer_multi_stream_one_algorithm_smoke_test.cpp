#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/source/source_session.h"

namespace {

using SectionMap = std::map<std::string, std::map<std::string, std::string>>;

std::string Trim(std::string value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string StripComment(std::string value) {
  const std::size_t comment = value.find('#');
  if (comment == std::string::npos) {
    return value;
  }
  return value.substr(0, comment);
}

std::string Unquote(std::string value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool LoadDocument(const std::string& file_path, SectionMap* sections, std::string* error) {
  std::ifstream input(file_path);
  if (!input) {
    if (error != nullptr) {
      *error = "failed to open config file: " + file_path;
    }
    return false;
  }

  std::string current_section;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const std::size_t indent = line.find_first_not_of(" ");
    const bool is_top_level = indent == 0 || indent == std::string::npos;
    std::string normalized = Trim(StripComment(line));
    if (normalized.empty()) {
      continue;
    }
    if (is_top_level && normalized.back() == ':' && normalized.find(':') == normalized.size() - 1) {
      current_section = Trim(normalized.substr(0, normalized.size() - 1));
      continue;
    }
    const std::size_t separator = normalized.find(':');
    if (separator == std::string::npos || current_section.empty()) {
      if (error != nullptr) {
        *error = "unsupported config syntax at line " + std::to_string(line_number);
      }
      return false;
    }
    const std::string key = Trim(normalized.substr(0, separator));
    const std::string value = Unquote(Trim(normalized.substr(separator + 1)));
    (*sections)[current_section][key] = value;
  }
  return true;
}

std::string GetValue(const SectionMap& sections,
                     const std::string& section,
                     const std::string& key,
                     const std::string& fallback) {
  const auto section_it = sections.find(section);
  if (section_it == sections.end()) {
    return fallback;
  }
  const auto value_it = section_it->second.find(key);
  if (value_it == section_it->second.end() || value_it->second.empty()) {
    return fallback;
  }
  return value_it->second;
}

int GetIntValue(const SectionMap& sections,
                const std::string& section,
                const std::string& key,
                int fallback) {
  const std::string value = GetValue(sections, section, key, "");
  if (value.empty()) {
    return fallback;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

bool IsStreamSection(const std::string& section) {
  return section.rfind("stream.", 0) == 0 && section.size() > std::string("stream.").size();
}

std::string StreamIdFromSection(const std::string& section) {
  return section.substr(std::string("stream.").size());
}

std::string EncodeDetectionResultJson(
    const std::string& source_session_id,
    int frame_id,
    const evr::algorithm::yolov8_person_detection::Detection& detection) {
  std::ostringstream out;
  out << "{\"source_session_id\":\"" << source_session_id
      << "\",\"decode_backend\":\"gstreamer\",\"frame_id\":" << frame_id
      << ",\"class_name\":\"" << detection.class_name << "\",\"score\":" << detection.score
      << "}";
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
  const std::string config_path =
      argc > 1 ? argv[1] : EVR_TEST_MULTI_STREAM_GSTREAMER_CONFIG_PATH;

  std::string error;
  SectionMap sections;
  if (!LoadDocument(config_path, &sections, &error)) {
    std::cerr << error << '\n';
    return 1;
  }

  const int frame_width = GetIntValue(sections, "test", "frame_width", 640);
  const int frame_height = GetIntValue(sections, "test", "frame_height", 360);
  const int frame_count = GetIntValue(sections, "test", "frame_count", 3);
  const std::string inference_backend =
      GetValue(sections, "test", "inference_backend", "synthetic");
  assert(frame_width > 0);
  assert(frame_height > 0);
  assert(frame_count > 0);

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  algorithm_config.backend = inference_backend;
  algorithm_config.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  algorithm_config.input_width = 640;
  algorithm_config.input_height = 640;
  algorithm_config.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  assert(detector.LoadModel(&error));

  std::size_t stream_count = 0;
  std::vector<std::string> encoded_results;
  for (const auto& [section, values] : sections) {
    (void)values;
    if (!IsStreamSection(section)) {
      continue;
    }
    ++stream_count;
    const std::string stream_id = StreamIdFromSection(section);

    evr::runtime::source::SourceSessionConfig source_config;
    source_config.session_id = stream_id;
    source_config.source_uri = GetValue(sections, section, "source_uri", "");
    source_config.upstream_kind =
        GetValue(sections, section, "upstream_kind", "direct-rtsp");
    source_config.transport_protocol =
        GetValue(sections, section, "transport_protocol", "rtsp");
    source_config.buffer_transport =
        GetValue(sections, section, "buffer_transport", "host-memory");
    source_config.decode_mode = GetValue(sections, section, "decode_mode", "gstreamer");
    source_config.pixel_format = GetValue(sections, section, "pixel_format", "rgba");
    source_config.decode_timeout_seconds =
        GetIntValue(sections, section, "decode_timeout_seconds", 10);
    source_config.decode_log_path =
        "/tmp/evr_runtime_gstreamer_multi_stream_" + stream_id + ".log";
    if (source_config.source_uri.empty()) {
      std::cerr << "stream " << stream_id << " has empty source_uri\n";
      return 1;
    }

    evr::runtime::source::SourceSession source_session;
    assert(source_session.Configure(source_config));
    assert(source_session.Start());

    const auto frames =
        source_session.CaptureFramesFromSource(frame_width, frame_height, frame_count, &error);
    if (frames.size() != static_cast<std::size_t>(frame_count)) {
      std::cerr << "stream " << stream_id << " failed: " << error << '\n';
      return 1;
    }

    for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
      const auto& frame = frames[static_cast<std::size_t>(frame_id)];
      const auto detections = detector.Detect(frame.rgba, frame.width, frame.height, &error);
      assert(!detections.empty());
      assert(detections.front().class_name == "person");
      encoded_results.push_back(
          EncodeDetectionResultJson(stream_id, frame_id, detections.front()));
    }
    source_session.Stop();
  }

  assert(stream_count >= 2U);
  assert(encoded_results.size() == stream_count * static_cast<std::size_t>(frame_count));
  assert(encoded_results.front().find("\"decode_backend\":\"gstreamer\"") != std::string::npos);
  assert(encoded_results.front().find("\"class_name\":\"person\"") != std::string::npos);
  return 0;
}
