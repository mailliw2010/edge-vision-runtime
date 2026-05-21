#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/source/source_session.h"

#if defined(EVR_WITH_LIBJPEG)
#include <jpeglib.h>
#endif

namespace {

bool CommandOk(const std::string& command) {
  return std::system(command.c_str()) == 0;
}

bool EnvFlagEnabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }
  const std::string flag(value);
  return flag == "1" || flag == "true" || flag == "TRUE" || flag == "yes" || flag == "YES";
}

std::string ReadTextFile(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
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

std::vector<std::uint8_t> RgbaToRgb(const std::vector<std::uint8_t>& rgba,
                                    int width,
                                    int height) {
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 3U);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t src = (static_cast<std::size_t>(y) * width + x) * 4U;
      const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 3U;
      rgb[dst + 0U] = rgba[src + 0U];
      rgb[dst + 1U] = rgba[src + 1U];
      rgb[dst + 2U] = rgba[src + 2U];
    }
  }
  return rgb;
}

int ClampToInt(float value, int min_value, int max_value) {
  return std::max(min_value, std::min(static_cast<int>(std::round(value)), max_value));
}

void DrawPixel(std::vector<std::uint8_t>* rgb, int width, int height, int x, int y) {
  if (rgb == nullptr || x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 3U;
  (*rgb)[offset + 0U] = 255U;
  (*rgb)[offset + 1U] = 32U;
  (*rgb)[offset + 2U] = 32U;
}

void DrawDetectionBox(std::vector<std::uint8_t>* rgb,
                      int width,
                      int height,
                      const evr::algorithm::yolov8_person_detection::Detection& detection) {
  const int x0 = ClampToInt(detection.x, 0, width - 1);
  const int y0 = ClampToInt(detection.y, 0, height - 1);
  const int x1 = ClampToInt(detection.x + detection.w, 0, width - 1);
  const int y1 = ClampToInt(detection.y + detection.h, 0, height - 1);
  constexpr int kThickness = 3;
  for (int t = 0; t < kThickness; ++t) {
    for (int x = x0; x <= x1; ++x) {
      DrawPixel(rgb, width, height, x, y0 + t);
      DrawPixel(rgb, width, height, x, y1 - t);
    }
    for (int y = y0; y <= y1; ++y) {
      DrawPixel(rgb, width, height, x0 + t, y);
      DrawPixel(rgb, width, height, x1 - t, y);
    }
  }
}

bool WriteJpeg(const std::filesystem::path& path,
               const std::vector<std::uint8_t>& rgb,
               int width,
               int height,
               int quality) {
#if !defined(EVR_WITH_LIBJPEG)
  (void)path;
  (void)rgb;
  (void)width;
  (void)height;
  (void)quality;
  std::cerr << "libjpeg is required to write JPEG event images\n";
  return false;
#else
  FILE* output = std::fopen(path.string().c_str(), "wb");
  if (output == nullptr) {
    return false;
  }

  jpeg_compress_struct compressor{};
  jpeg_error_mgr error_manager{};
  compressor.err = jpeg_std_error(&error_manager);
  jpeg_create_compress(&compressor);
  jpeg_stdio_dest(&compressor, output);
  compressor.image_width = static_cast<JDIMENSION>(width);
  compressor.image_height = static_cast<JDIMENSION>(height);
  compressor.input_components = 3;
  compressor.in_color_space = JCS_RGB;
  jpeg_set_defaults(&compressor);
  jpeg_set_quality(&compressor, quality, TRUE);
  jpeg_start_compress(&compressor, TRUE);

  const auto row_stride = static_cast<std::size_t>(width) * 3U;
  while (compressor.next_scanline < compressor.image_height) {
    JSAMPROW row = const_cast<JSAMPROW>(
        &rgb[static_cast<std::size_t>(compressor.next_scanline) * row_stride]);
    jpeg_write_scanlines(&compressor, &row, 1);
  }

  jpeg_finish_compress(&compressor);
  jpeg_destroy_compress(&compressor);
  std::fclose(output);
  return true;
#endif
}

std::filesystem::path FrameEventPath(const std::filesystem::path& event_dir,
                                     int frame_id,
                                     const std::string& suffix) {
  std::ostringstream name;
  name << "frame_" << std::setw(3) << std::setfill('0') << frame_id << suffix;
  return event_dir / name.str();
}

bool WriteEventJson(
    const std::filesystem::path& path,
    int frame_id,
    const std::string& source_uri,
    const std::filesystem::path& image_path,
    int width,
    int height,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections) {
  std::ofstream output(path);
  if (!output) {
    return false;
  }

  output << "{\n";
  output << "  \"frame_id\": " << frame_id << ",\n";
  output << "  \"source_uri\": \"" << RedactUri(source_uri) << "\",\n";
  output << "  \"image\": {\n";
  output << "    \"format\": \"jpeg\",\n";
  output << "    \"width\": " << width << ",\n";
  output << "    \"height\": " << height << ",\n";
  output << "    \"path\": \"" << image_path.string() << "\"\n";
  output << "  },\n";
  output << "  \"detections\": [\n";
  for (std::size_t i = 0; i < detections.size(); ++i) {
    const auto& detection = detections[i];
    output << "    {\"class_id\": " << detection.class_id << ", \"class_name\": \""
           << detection.class_name << "\", \"score\": " << detection.score
           << ", \"box\": {\"x\": " << detection.x << ", \"y\": " << detection.y
           << ", \"w\": " << detection.w << ", \"h\": " << detection.h << "}}";
    output << (i + 1U == detections.size() ? "\n" : ",\n");
  }
  output << "  ]\n";
  output << "}\n";
  return static_cast<bool>(output);
}

bool PersistLocalEvent(
    const std::filesystem::path& event_dir,
    int frame_id,
    const std::string& source_uri,
    const std::vector<std::uint8_t>& rgba_frame,
    int width,
    int height,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
    bool render_boxes) {
  if (event_dir.empty()) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(event_dir, ec);
  if (ec) {
    std::cerr << "failed to create event dir: " << event_dir << ": " << ec.message() << '\n';
    return false;
  }

  const auto image_path = FrameEventPath(event_dir, frame_id, ".jpg");
  const auto event_path = FrameEventPath(event_dir, frame_id, "_event.json");
  auto rgb = RgbaToRgb(rgba_frame, width, height);
  if (render_boxes) {
    for (const auto& detection : detections) {
      DrawDetectionBox(&rgb, width, height, detection);
    }
  }
  if (!WriteJpeg(image_path, rgb, width, height, 90)) {
    std::cerr << "failed to write event image to " << image_path << '\n';
    return false;
  }
  if (!WriteEventJson(event_path, frame_id, source_uri, image_path, width, height, detections)) {
    std::cerr << "failed to write event json to " << event_path << '\n';
    return false;
  }
  return true;
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
  const char* event_dir_env = std::getenv("EVR_TEST_EVENT_DIR");
  if (event_dir_env == nullptr || std::string(event_dir_env).empty()) {
    event_dir_env = std::getenv("EVR_TEST_DUMP_DIR");
  }
  const std::filesystem::path event_dir =
      event_dir_env == nullptr ? std::filesystem::path{} : std::filesystem::path(event_dir_env);
  const bool save_negative_frames = EnvFlagEnabled("EVR_TEST_SAVE_NEGATIVE_FRAMES");
  const int frame_width = 640;
  const int frame_height = 360;
  const int frame_count = 3;
  const int ffmpeg_timeout_seconds = 60;

  evr::runtime::source::SourceSession source_session;
  evr::runtime::source::SourceSessionConfig source_config;
  source_config.session_id = source_label + "-video-source-smoke";
  source_config.source_uri = rtsp_uri;
  source_config.upstream_kind = source_label == "zlm" ? "zlm-proxy" : "direct-rtsp";
  source_config.transport_protocol = "rtsp";
  source_config.buffer_transport = "host-memory";
  source_config.decode_mode = "ffmpeg";
  source_config.pixel_format = "rgba";
  source_config.decode_timeout_seconds = ffmpeg_timeout_seconds;
  source_config.decode_log_path = "/tmp/evr_runtime_" + source_label +
                                  "_video_detect_result_ffmpeg.log";
  assert(source_session.Configure(source_config));
  assert(source_session.Start());

  std::string error;
  const auto frames = source_session.CaptureFramesFromSource(frame_width, frame_height, frame_count,
                                                            &error);
  if (frames.size() != static_cast<std::size_t>(frame_count)) {
    const std::string ffmpeg_log = ReadTextFile(source_config.decode_log_path);
    std::cerr << error << '\n';
    if (!ffmpeg_log.empty()) {
      std::cerr << "ffmpeg stderr:\n" << ffmpeg_log;
    }
    return 1;
  }

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  const char* backend_env = std::getenv("EVR_TEST_INFERENCE_BACKEND");
  algorithm_config.backend =
      backend_env == nullptr || std::string(backend_env).empty() ? "synthetic" : backend_env;
  const char* model_path_env = std::getenv("EVR_TEST_MODEL_PATH");
  algorithm_config.model_path =
      model_path_env == nullptr || std::string(model_path_env).empty()
          ? EVR_TEST_YOLOV8S_MODEL_PATH
          : model_path_env;
  algorithm_config.input_width = 640;
  algorithm_config.input_height = 640;
  algorithm_config.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  if (!detector.LoadModel(&error)) {
    std::cerr << "failed to load detector model with backend=" << algorithm_config.backend
              << ", model_path=" << algorithm_config.model_path << ": " << error << '\n';
    const std::string tensorrt_log = ReadTextFile("/tmp/evr_tensorrt_build.log");
    if (!tensorrt_log.empty()) {
      std::cerr << "TensorRT build log:\n" << tensorrt_log;
    }
    return 1;
  }

  std::vector<std::string> encoded_results;
  for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
    const auto& frame = frames[static_cast<std::size_t>(frame_id)];
    const auto detections =
        detector.DetectImage(frame.bytes, frame.width, frame.height, frame.pixel_format, &error);
    if (detections.empty()) {
      std::cerr << "no detections for frame " << frame_id << " with backend="
                << algorithm_config.backend << ": " << error << '\n';
      if (save_negative_frames) {
        assert(PersistLocalEvent(event_dir, frame_id, rtsp_uri, frame.bytes, frame.width,
                                 frame.height, detections, false));
      }
      continue;
    }
    if (detections.front().class_name != "person") {
      std::cerr << "unexpected first detection class: " << detections.front().class_name << '\n';
      return 1;
    }
    encoded_results.push_back(EncodeDetectionResultJson(frame_id, rtsp_uri, detections.front()));
    assert(PersistLocalEvent(event_dir, frame_id, rtsp_uri, frame.bytes, frame.width, frame.height,
                             detections, true));
  }

  if (encoded_results.empty()) {
    std::cerr << "no detection events were produced";
    if (save_negative_frames && !event_dir.empty()) {
      std::cerr << "; negative frames were saved to " << event_dir;
    }
    std::cerr << '\n';
    return 1;
  }

  assert(encoded_results.size() == static_cast<std::size_t>(frame_count));
  assert(encoded_results.front().find("\"frame_id\":0") != std::string::npos);
  assert(encoded_results.front().find("\"source_uri\":\"rtsp://") != std::string::npos);
  assert(encoded_results.front().find("\"class_name\":\"person\"") != std::string::npos);

  source_session.Stop();
  return 0;
}
