#include "evr/runtime/worker/worker_app.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/session/session_state.h"

#if defined(EVR_WITH_LIBJPEG)
#include <jpeglib.h>
#endif

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

std::string JsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::int64_t UnixTimeMillis() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

double SecondsSince(const std::chrono::steady_clock::time_point& start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
}

std::filesystem::path ResolveObservePreviewDir(const WorkerAppConfig& config) {
  if (!config.observability.preview_dir.empty()) {
    return std::filesystem::path(config.observability.preview_dir) / config.source.session_id;
  }
  if (!config.event_store_dir.empty()) {
    return std::filesystem::path(config.event_store_dir) / "_observe_previews" /
           config.source.session_id;
  }
  if (!config.dump_dir.empty()) {
    return std::filesystem::path(config.dump_dir) / "_observe_previews" /
           config.source.session_id;
  }
  return std::filesystem::temp_directory_path() / "evr_observe_previews" / config.source.session_id;
}

std::filesystem::path ObservePreviewFramePath(const std::filesystem::path& preview_dir,
                                             std::size_t frame_id) {
  std::ostringstream name;
  name << "frame_" << std::setw(3) << std::setfill('0') << frame_id << ".jpg";
  return preview_dir / name.str();
}

std::filesystem::path ObservePreviewLatestPath(const std::filesystem::path& preview_dir) {
  return preview_dir / "latest.jpg";
}

std::filesystem::path ObserveMetadataFramePath(const std::filesystem::path& preview_dir,
                                               std::size_t frame_id) {
  std::ostringstream name;
  name << "frame_" << std::setw(3) << std::setfill('0') << frame_id << ".osd.json";
  return preview_dir / name.str();
}

std::filesystem::path ObserveMetadataLatestPath(const std::filesystem::path& preview_dir) {
  return preview_dir / "latest.osd.json";
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

int ClampToInt(float value, int min_value, int max_value) {
  return std::max(min_value, std::min(static_cast<int>(std::round(value)), max_value));
}

std::vector<std::uint8_t> RgbaToRgb(const std::vector<std::uint8_t>& rgba,
                                    int width,
                                    int height) {
  std::vector<std::uint8_t> rgb;
  rgb.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
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

int ClampByte(int value) {
  return std::max(0, std::min(value, 255));
}

std::vector<std::uint8_t> Nv12ToRgb(const std::vector<std::uint8_t>& nv12,
                                    int width,
                                    int height) {
  std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) * 3U);
  const std::size_t y_plane_size =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (nv12.size() < y_plane_size * 3U / 2U) {
    return {};
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t y_index = static_cast<std::size_t>(y) * width + x;
      const std::size_t uv_index =
          y_plane_size + static_cast<std::size_t>(y / 2) * width +
          static_cast<std::size_t>(x / 2) * 2U;
      const int y_value = static_cast<int>(nv12[y_index]);
      const int u_value = static_cast<int>(nv12[uv_index + 0U]);
      const int v_value = static_cast<int>(nv12[uv_index + 1U]);
      const int c = std::max(0, y_value - 16);
      const int d = u_value - 128;
      const int e = v_value - 128;
      const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 3U;
      rgb[dst + 0U] = static_cast<std::uint8_t>(ClampByte((298 * c + 409 * e + 128) >> 8));
      rgb[dst + 1U] = static_cast<std::uint8_t>(ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8));
      rgb[dst + 2U] = static_cast<std::uint8_t>(ClampByte((298 * c + 516 * d + 128) >> 8));
    }
  }
  return rgb;
}

std::vector<std::uint8_t> FrameToRgb(const source::FrameBuffer& frame) {
  if (frame.pixel_format == "rgba" || frame.pixel_format == "RGBA") {
    return RgbaToRgb(frame.bytes, frame.width, frame.height);
  }
  if (frame.pixel_format == "nv12" || frame.pixel_format == "NV12") {
    return Nv12ToRgb(frame.bytes, frame.width, frame.height);
  }
  return {};
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

bool WritePpm(const std::filesystem::path& path,
              const std::vector<std::uint8_t>& rgb,
              int width,
              int height,
              std::string* error) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    if (error != nullptr) {
      *error = "failed to open dump image: " + path.string();
    }
    return false;
  }
  output << "P6\n" << width << " " << height << "\n255\n";
  output.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  return static_cast<bool>(output);
}

bool WriteJpeg(const std::filesystem::path& path,
               const std::vector<std::uint8_t>& rgb,
               int width,
               int height,
               int quality,
               std::string* error) {
#if !defined(EVR_WITH_LIBJPEG)
  (void)path;
  (void)rgb;
  (void)width;
  (void)height;
  (void)quality;
  if (error != nullptr) {
    *error = "libjpeg is required to write event JPEG images";
  }
  return false;
#else
  FILE* output = std::fopen(path.string().c_str(), "wb");
  if (output == nullptr) {
    if (error != nullptr) {
      *error = "failed to open JPEG image: " + path.string();
    }
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

std::filesystem::path FrameDumpPath(const std::filesystem::path& dump_dir,
                                    std::size_t frame_id,
                                    const std::string& suffix) {
  std::ostringstream name;
  name << "frame_" << std::setw(3) << std::setfill('0') << frame_id << suffix << ".ppm";
  return dump_dir / name.str();
}

bool DumpFrameAndDetections(
    const std::filesystem::path& dump_dir,
    std::size_t frame_id,
    const source::FrameBuffer& frame,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
    std::string* error) {
  if (dump_dir.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(dump_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create dump dir: " + dump_dir.string() + ": " + ec.message();
    }
    return false;
  }

  auto rgb = FrameToRgb(frame);
  if (rgb.empty()) {
    if (error != nullptr) {
      *error = "unsupported frame format for dump: " + frame.pixel_format;
    }
    return false;
  }
  if (!WritePpm(FrameDumpPath(dump_dir, frame_id, ""), rgb, frame.width, frame.height, error)) {
    return false;
  }
  auto boxed = rgb;
  for (const auto& detection : detections) {
    DrawDetectionBox(&boxed, frame.width, frame.height, detection);
  }
  return WritePpm(FrameDumpPath(dump_dir, frame_id, "_bbox"), boxed, frame.width, frame.height,
                  error);
}

std::string BuildEventId(const WorkerAppConfig& config,
                         std::size_t frame_id,
                         std::int64_t event_time_ms) {
  std::ostringstream out;
  out << config.worker.session_id << "_frame_" << std::setw(6) << std::setfill('0') << frame_id
      << "_" << event_time_ms;
  return out.str();
}

bool WriteObserveSidecarJson(
    const std::filesystem::path& path,
    const WorkerAppConfig& config,
    std::size_t frame_id,
    std::int64_t ts_ms,
    const source::FrameBuffer& frame,
    const std::filesystem::path& preview_path,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
    const observability::TimingBreakdown& timing,
    std::string* error) {
  std::ofstream output(path);
  if (!output) {
    if (error != nullptr) {
      *error = "failed to open observe sidecar json: " + path.string();
    }
    return false;
  }

  output << "{\n";
  output << "  \"schema_version\": \"v1\",\n";
  output << "  \"producer\": {\n";
  output << "    \"name\": \"edge-vision-runtime\",\n";
  output << "    \"version\": \"0.1.0\"\n";
  output << "  },\n";
  output << "  \"type\": \"frontend_osd_frame\",\n";
  output << "  \"source_session_id\": \"" << JsonEscape(config.source.session_id) << "\",\n";
  output << "  \"frame_id\": " << frame_id << ",\n";
  output << "  \"ts_ms\": " << ts_ms << ",\n";
  output << "  \"decode_mode\": \"" << JsonEscape(config.source.decode_mode) << "\",\n";
  output << "  \"pixel_format\": \"" << JsonEscape(frame.pixel_format) << "\",\n";
  output << "  \"buffer_transport\": \"" << JsonEscape(frame.buffer_transport) << "\",\n";
  output << "  \"osd_mode\": \"frontend-overlay\",\n";
  output << "  \"boxes_burned_in\": false,\n";
  output << "  \"preview\": {\n";
  output << "    \"format\": \"jpeg\",\n";
  output << "    \"path\": \"" << JsonEscape(preview_path.string()) << "\",\n";
  output << "    \"width\": " << frame.width << ",\n";
  output << "    \"height\": " << frame.height << "\n";
  output << "  },\n";
  output << "  \"timing\": {\n";
  output << "    \"decode_ms\": " << timing.decode_ms << ",\n";
  output << "    \"preprocess_ms\": " << timing.preprocess_ms << ",\n";
  output << "    \"infer_ms\": " << timing.infer_ms << ",\n";
  output << "    \"postprocess_ms\": " << timing.postprocess_ms << ",\n";
  output << "    \"e2e_ms\": " << timing.e2e_ms << "\n";
  output << "  },\n";
  output << "  \"detections\": [\n";
  for (std::size_t i = 0; i < detections.size(); ++i) {
    const auto& detection = detections[i];
    output << "    {\"class_id\": " << detection.class_id << ", \"class_name\": \""
           << JsonEscape(detection.class_name) << "\", \"score\": " << detection.score
           << ", \"box\": {\"x\": " << detection.x << ", \"y\": " << detection.y
           << ", \"w\": " << detection.w << ", \"h\": " << detection.h << "}}";
    output << (i + 1U == detections.size() ? "\n" : ",\n");
  }
  output << "  ]\n";
  output << "}\n";
  return static_cast<bool>(output);
}

bool PersistObservePreview(const WorkerAppConfig& config,
                           std::size_t frame_id,
                           std::int64_t ts_ms,
                           const source::FrameBuffer& frame,
                           const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
                           const observability::TimingBreakdown& timing,
                           std::filesystem::path* preview_path,
                           std::filesystem::path* metadata_path,
                           std::string* error) {
  if (!config.observability.enabled) {
    return true;
  }

  const auto preview_dir = ResolveObservePreviewDir(config);
  std::error_code ec;
  std::filesystem::create_directories(preview_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create observe preview dir: " + preview_dir.string() + ": " +
               ec.message();
    }
    return false;
  }

  auto rgb = FrameToRgb(frame);
  if (rgb.empty()) {
    if (error != nullptr) {
      *error = "unsupported frame format for observe preview: " + frame.pixel_format;
    }
    return false;
  }

  const auto frame_path = ObservePreviewFramePath(preview_dir, frame_id);
  if (!WriteJpeg(frame_path, rgb, frame.width, frame.height, 90, error)) {
    return false;
  }
  const auto latest_path = ObservePreviewLatestPath(preview_dir);
  std::filesystem::copy_file(frame_path, latest_path, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to write observe latest preview: " + latest_path.string() + ": " +
               ec.message();
    }
    return false;
  }

  const auto frame_metadata_path = ObserveMetadataFramePath(preview_dir, frame_id);
  const auto latest_metadata_path = ObserveMetadataLatestPath(preview_dir);
  if (!WriteObserveSidecarJson(frame_metadata_path, config, frame_id, ts_ms, frame, frame_path,
                               detections, timing, error)) {
    return false;
  }
  std::filesystem::copy_file(frame_metadata_path, latest_metadata_path,
                             std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to write observe latest sidecar: " + latest_metadata_path.string() +
               ": " + ec.message();
    }
    return false;
  }

  if (preview_path != nullptr) {
    *preview_path = latest_path;
  }
  if (metadata_path != nullptr) {
    *metadata_path = latest_metadata_path;
  }
  return true;
}

bool WriteDetectionEventJson(
    const std::filesystem::path& path,
    const WorkerAppConfig& config,
    const std::string& event_id,
    std::size_t frame_id,
    std::int64_t event_time_ms,
    const source::FrameBuffer& frame,
    const std::filesystem::path& image_path,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
    std::string* error) {
  std::ofstream output(path);
  if (!output) {
    if (error != nullptr) {
      *error = "failed to open event json: " + path.string();
    }
    return false;
  }

  output << "{\n";
  output << "  \"event_id\": \"" << JsonEscape(event_id) << "\",\n";
  output << "  \"event_time_ms\": " << event_time_ms << ",\n";
  output << "  \"source_session_id\": \"" << JsonEscape(config.source.session_id) << "\",\n";
  output << "  \"worker_session_id\": \"" << JsonEscape(config.worker.session_id) << "\",\n";
  output << "  \"source_uri\": \"" << JsonEscape(RedactUri(config.source.source_uri)) << "\",\n";
  output << "  \"inference_backend\": \"" << JsonEscape(config.worker.inference_backend) << "\",\n";
  output << "  \"algorithm_name\": \"" << JsonEscape(config.worker.algorithm_name) << "\",\n";
  output << "  \"model_path\": \"" << JsonEscape(config.worker.engine_path) << "\",\n";
  output << "  \"output_topic\": \"" << JsonEscape(config.worker.output_topic) << "\",\n";
  output << "  \"frame\": {\n";
  output << "    \"frame_id\": " << frame_id << ",\n";
  output << "    \"width\": " << frame.width << ",\n";
  output << "    \"height\": " << frame.height << ",\n";
  output << "    \"format\": \"jpeg\",\n";
  output << "    \"image_path\": \"" << JsonEscape(image_path.string()) << "\"\n";
  output << "  },\n";
  output << "  \"detections\": [\n";
  for (std::size_t i = 0; i < detections.size(); ++i) {
    const auto& detection = detections[i];
    output << "    {\"class_id\": " << detection.class_id << ", \"class_name\": \""
           << JsonEscape(detection.class_name) << "\", \"score\": " << detection.score
           << ", \"box\": {\"x\": " << detection.x << ", \"y\": " << detection.y
           << ", \"w\": " << detection.w << ", \"h\": " << detection.h << "}}";
    output << (i + 1U == detections.size() ? "\n" : ",\n");
  }
  output << "  ]\n";
  output << "}\n";
  return static_cast<bool>(output);
}

bool PersistDetectionEvent(
    const std::filesystem::path& event_store_dir,
    std::size_t frame_id,
    const WorkerAppConfig& config,
    const source::FrameBuffer& frame,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
    std::string* error) {
  if (event_store_dir.empty()) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(event_store_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create event store dir: " + event_store_dir.string() + ": " +
               ec.message();
    }
    return false;
  }

  const auto event_time_ms = UnixTimeMillis();
  const auto event_id = BuildEventId(config, frame_id, event_time_ms);
  const auto event_dir = event_store_dir / event_id;
  std::filesystem::create_directories(event_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create event dir: " + event_dir.string() + ": " + ec.message();
    }
    return false;
  }

  const auto image_path = event_dir / "frame.jpg";
  auto rgb = FrameToRgb(frame);
  if (rgb.empty()) {
    if (error != nullptr) {
      *error = "unsupported frame format for event persistence: " + frame.pixel_format;
    }
    return false;
  }
  if (!WriteJpeg(image_path, rgb, frame.width, frame.height, 90, error)) {
    return false;
  }

  const auto event_json_path = event_dir / "event.json";
  if (!WriteDetectionEventJson(event_json_path, config, event_id, frame_id, event_time_ms, frame,
                               image_path, detections, error)) {
    return false;
  }

  std::cout << "event_persisted[id=" << event_id << ", path=" << event_json_path.string()
            << "]\n";
  return true;
}

bool PersistNegativeFrame(
    const std::filesystem::path& negative_store_dir,
    std::size_t frame_id,
    const WorkerAppConfig& config,
    const source::FrameBuffer& frame,
    std::string* error) {
  if (negative_store_dir.empty()) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(negative_store_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create negative frame dir: " + negative_store_dir.string() + ": " +
               ec.message();
    }
    return false;
  }

  const auto event_time_ms = UnixTimeMillis();
  const auto event_id = BuildEventId(config, frame_id, event_time_ms) + "_negative";
  const auto event_dir = negative_store_dir / event_id;
  std::filesystem::create_directories(event_dir, ec);
  if (ec) {
    if (error != nullptr) {
      *error = "failed to create negative frame event dir: " + event_dir.string() + ": " +
               ec.message();
    }
    return false;
  }

  const auto image_path = event_dir / "frame.jpg";
  auto rgb = FrameToRgb(frame);
  if (rgb.empty()) {
    if (error != nullptr) {
      *error = "unsupported frame format for negative-frame persistence: " + frame.pixel_format;
    }
    return false;
  }
  if (!WriteJpeg(image_path, rgb, frame.width, frame.height, 90, error)) {
    return false;
  }

  const std::vector<evr::algorithm::yolov8_person_detection::Detection> detections;
  const auto event_json_path = event_dir / "event.json";
  if (!WriteDetectionEventJson(event_json_path, config, event_id, frame_id, event_time_ms, frame,
                               image_path, detections, error)) {
    return false;
  }

  std::cout << "negative_frame_persisted[id=" << event_id
            << ", path=" << event_json_path.string() << "]\n";
  return true;
}

void PublishObserveRuntimeEvent(
    observability::ObservePublisher* publisher,
    const WorkerAppConfig& config,
    const std::string& level,
    const std::string& event_name,
    const std::string& message) {
  if (publisher == nullptr || !config.observability.enabled) {
    return;
  }
  observability::RuntimeEventRecord event;
  event.level = level;
  event.stream_id = config.source.session_id;
  event.event = event_name;
  event.message = message;
  event.ts_ms = UnixTimeMillis();
  std::string error;
  if (!publisher->PublishRuntimeEvent(event, &error)) {
    std::cerr << "observe publish runtime_event failed: " << error << std::endl;
  }
}

void PublishObserveFrame(
    observability::ObservePublisher* publisher,
    const WorkerAppConfig& config,
    const source::FrameBuffer& frame,
    std::size_t frame_id,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections,
    const observability::TimingBreakdown& timing,
    const std::filesystem::path& preview_path,
    const std::filesystem::path& metadata_path,
    const std::chrono::steady_clock::time_point& start_time) {
  if (publisher == nullptr || !config.observability.enabled) {
    return;
  }

  const std::int64_t ts_ms = UnixTimeMillis();
  observability::FrontendOsdFrameEvent frame_event;
  frame_event.source_session_id = config.source.session_id;
  frame_event.frame_id = static_cast<std::uint64_t>(frame_id);
  frame_event.ts_ms = ts_ms;
  frame_event.decode_mode = config.source.decode_mode;
  frame_event.pixel_format = frame.pixel_format;
  frame_event.buffer_transport = frame.buffer_transport;
  frame_event.preview_path = preview_path.string();
  frame_event.preview_width = frame.width;
  frame_event.preview_height = frame.height;
  frame_event.timing = timing;
  for (const auto& detection : detections) {
    frame_event.detections.push_back(
        {detection.class_id, detection.class_name, detection.score,
         ClampToInt(detection.x, 0, frame.width),
         ClampToInt(detection.y, 0, frame.height),
         ClampToInt(detection.w, 0, frame.width),
         ClampToInt(detection.h, 0, frame.height)});
  }

  const double elapsed_seconds = SecondsSince(start_time);
  const double infer_fps =
      elapsed_seconds > 0.0 ? (static_cast<double>(frame_id + 1U) / elapsed_seconds) : 0.0;
  observability::StreamSnapshotEvent stream_snapshot;
  stream_snapshot.stream_id = config.source.session_id;
  stream_snapshot.source_session_id = config.source.session_id;
  stream_snapshot.decode_mode = config.source.decode_mode;
  stream_snapshot.pixel_format = frame.pixel_format;
  stream_snapshot.buffer_transport = frame.buffer_transport;
  stream_snapshot.preview_path = preview_path.string();
  stream_snapshot.metadata_path = metadata_path.string();
  stream_snapshot.input_fps = infer_fps;
  stream_snapshot.decoded_fps = infer_fps;
  stream_snapshot.infer_fps = infer_fps;
  stream_snapshot.queue_depth = 0;
  stream_snapshot.drop_total = 0;
  stream_snapshot.frame_age_ms = 0;
  stream_snapshot.last_detection_count = static_cast<int>(detections.size());
  stream_snapshot.healthy = true;
  stream_snapshot.latency = timing;

  std::string error;
  if (!publisher->PublishFrontendOsdFrame(frame_event, &error)) {
    std::cerr << "observe publish frontend_osd_frame failed: " << error << std::endl;
  }
  if (!publisher->PublishStreamSnapshot(stream_snapshot, &error)) {
    std::cerr << "observe publish stream_snapshot failed: " << error << std::endl;
  }
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
  if (!config_.worker.inference_backend.empty()) {
    algorithm_config.backend = config_.worker.inference_backend;
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
  observability::ObservePublisher observe_publisher;

  if (config_.observability.enabled) {
    observability::ObservePublisherConfig observe_config;
    observe_config.enabled = true;
    observe_config.socket_path = config_.observability.socket_path;
    if (!observe_publisher.Configure(observe_config, &algorithm_error)) {
      std::cerr << algorithm_error << std::endl;
      return 1;
    }
  }

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
  const auto run_start = std::chrono::steady_clock::now();
  for (std::size_t frame_id = 0; frame_id < frames.size(); ++frame_id) {
    const auto& frame = frames[frame_id];
    const auto frame_start = std::chrono::steady_clock::now();
    const auto detections =
        detector.DetectImage(frame.bytes, frame.width, frame.height, frame.pixel_format,
                             &algorithm_error);
    const auto frame_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - frame_start);
    observability::TimingBreakdown timing;
    timing.infer_ms = static_cast<int>(frame_elapsed.count());
    timing.e2e_ms = static_cast<int>(frame_elapsed.count());
    if (detections.empty()) {
      PublishObserveRuntimeEvent(&observe_publisher, config_, "warn", "no_detections",
                                 "no detections for frame " + std::to_string(frame_id));
      if (config_.dump_negative_frames) {
        std::filesystem::path negative_dir;
        if (!config_.dump_dir.empty()) {
          negative_dir = std::filesystem::path(config_.dump_dir) / "_negative_frames";
        } else if (!config_.event_store_dir.empty()) {
          negative_dir = std::filesystem::path(config_.event_store_dir) / "_negative_frames";
        }
        if (!PersistNegativeFrame(negative_dir, frame_id, config_, frame, &algorithm_error)) {
          std::cerr << algorithm_error << std::endl;
        }
      }
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

    std::filesystem::path observe_preview_path;
    std::filesystem::path observe_metadata_path;
    if (config_.observability.enabled &&
        !PersistObservePreview(config_, frame_id, UnixTimeMillis(), frame, detections, timing,
                               &observe_preview_path, &observe_metadata_path, &algorithm_error)) {
      std::cerr << algorithm_error << std::endl;
      worker_session.Stop();
      source_session.Stop();
      return 1;
    }

    if (!config_.dump_dir.empty() &&
        !DumpFrameAndDetections(config_.dump_dir, frame_id, frame, detections, &algorithm_error)) {
      std::cerr << algorithm_error << std::endl;
      worker_session.Stop();
      source_session.Stop();
      return 1;
    }
    if (!config_.event_store_dir.empty() &&
        !PersistDetectionEvent(config_.event_store_dir, frame_id, config_, frame, detections,
                               &algorithm_error)) {
      std::cerr << algorithm_error << std::endl;
      worker_session.Stop();
      source_session.Stop();
      return 1;
    }

    PublishObserveFrame(&observe_publisher, config_, frame, frame_id, detections, timing,
                        observe_preview_path, observe_metadata_path, run_start);
  }

  worker_session.Stop();
  source_session.Stop();
  return 0;
}

}  // namespace evr::runtime::worker
