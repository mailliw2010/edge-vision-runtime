#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "evr/algorithm/yolov8_person_detection/yolov8_person_detector.h"
#include "evr/runtime/source/source_session.h"

#if defined(EVR_WITH_LIBJPEG)
#include <jpeglib.h>
#endif

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

bool IsRtspUri(const std::string& uri) {
  return uri.rfind("rtsp://", 0) == 0 || uri.rfind("rtsps://", 0) == 0;
}

bool PathExists(const char* path) {
  return access(path, F_OK) == 0;
}

bool HasJetsonDeviceRuntimePrerequisites() {
  const bool has_nvmap = PathExists("/dev/nvmap");
  const bool has_decode_node = PathExists("/dev/nvhost-nvdec") || PathExists("/dev/v4l-subdev0") ||
                               PathExists("/dev/video0");
  const bool has_vic_node = PathExists("/dev/nvhost-vic") || PathExists("/dev/nvhost-ctrl");
  return has_nvmap && has_decode_node && has_vic_node;
}

std::filesystem::path ResolveOutputDir(int argc, char** argv) {
  if (argc > 2 && argv[2] != nullptr && std::string(argv[2]).size() > 0U) {
    return std::filesystem::path(argv[2]);
  }
  return std::filesystem::temp_directory_path() / "evr_runtime_frontend_osd_smoke";
}

int ClampByte(int value) {
  return std::max(0, std::min(value, 255));
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

std::vector<std::uint8_t> Nv12ToRgb(const std::vector<std::uint8_t>& nv12,
                                    int width,
                                    int height) {
  const std::size_t y_plane_size =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (nv12.size() < y_plane_size * 3U / 2U) {
    return {};
  }
  std::vector<std::uint8_t> rgb(y_plane_size * 3U);
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

std::vector<std::uint8_t> FrameToRgb(const evr::runtime::source::FrameBuffer& frame) {
  if (frame.pixel_format == "rgba" || frame.pixel_format == "RGBA") {
    return RgbaToRgb(frame.bytes, frame.width, frame.height);
  }
  if (frame.pixel_format == "nv12" || frame.pixel_format == "NV12") {
    return Nv12ToRgb(frame.bytes, frame.width, frame.height);
  }
  return {};
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
  std::cerr << "libjpeg is required for runtime_frontend_osd_multi_stream_smoke_test\n";
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

std::filesystem::path PreviewPath(const std::filesystem::path& stream_dir, int frame_id) {
  std::ostringstream name;
  name << "frame_" << std::setw(3) << std::setfill('0') << frame_id << ".jpg";
  return stream_dir / name.str();
}

std::filesystem::path MetadataPath(const std::filesystem::path& stream_dir, int frame_id) {
  std::ostringstream name;
  name << "frame_" << std::setw(3) << std::setfill('0') << frame_id << ".osd.json";
  return stream_dir / name.str();
}

bool WriteFrontendOsdJson(
    const std::filesystem::path& path,
    const std::string& source_session_id,
    int frame_id,
    const std::string& decode_mode,
    const evr::runtime::source::FrameBuffer& frame,
    const std::filesystem::path& preview_path,
    const std::vector<evr::algorithm::yolov8_person_detection::Detection>& detections) {
  std::ofstream output(path);
  if (!output) {
    return false;
  }

  output << "{\n";
  output << "  \"source_session_id\": \"" << source_session_id << "\",\n";
  output << "  \"frame_id\": " << frame_id << ",\n";
  output << "  \"decode_mode\": \"" << decode_mode << "\",\n";
  output << "  \"pixel_format\": \"" << frame.pixel_format << "\",\n";
  output << "  \"buffer_transport\": \"" << frame.buffer_transport << "\",\n";
  output << "  \"osd_mode\": \"frontend-overlay\",\n";
  output << "  \"boxes_burned_in\": false,\n";
  output << "  \"preview\": {\n";
  output << "    \"format\": \"jpeg\",\n";
  output << "    \"path\": \"" << preview_path.string() << "\",\n";
  output << "    \"width\": " << frame.width << ",\n";
  output << "    \"height\": " << frame.height << "\n";
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

std::string ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(EVR_WITH_LIBJPEG)
  std::cerr << "libjpeg is required for runtime_frontend_osd_multi_stream_smoke_test\n";
  return 77;
#else
  const std::string config_path = argc > 1
                                      ? argv[1]
                                      : EVR_TEST_MULTI_STREAM_GSTREAMER_NV12_HOST_CONFIG_PATH;
  const std::filesystem::path output_dir = ResolveOutputDir(argc, argv);

  std::error_code ec;
  std::filesystem::remove_all(output_dir, ec);
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::cerr << "failed to create output dir: " << output_dir << ": " << ec.message() << '\n';
    return 1;
  }

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

  evr::algorithm::yolov8_person_detection::AlgorithmConfig algorithm_config;
  algorithm_config.backend = inference_backend;
  algorithm_config.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  algorithm_config.input_width = 640;
  algorithm_config.input_height = 640;
  algorithm_config.confidence_threshold = 0.25F;

  evr::algorithm::yolov8_person_detection::YoloV8PersonDetector detector(algorithm_config);
  assert(detector.LoadModel(&error));

  std::size_t stream_count = 0;
  std::string first_decode_mode;
  int artifacts_written = 0;
  for (const auto& [section, values] : sections) {
    (void)values;
    if (!IsStreamSection(section)) {
      continue;
    }
    ++stream_count;
    const std::string stream_id = StreamIdFromSection(section);
    const std::filesystem::path stream_dir = output_dir / stream_id;
    std::filesystem::create_directories(stream_dir, ec);
    if (ec) {
      std::cerr << "failed to create stream dir: " << stream_dir << ": " << ec.message() << '\n';
      return 1;
    }

    evr::runtime::source::SourceSessionConfig source_config;
    source_config.session_id = stream_id;
    source_config.source_uri = GetValue(sections, section, "source_uri", "");
    source_config.upstream_kind =
        GetValue(sections, section, "upstream_kind", "gstreamer-testsrc");
    source_config.transport_protocol =
        GetValue(sections, section, "transport_protocol", "in-process");
    source_config.buffer_transport =
        GetValue(sections, section, "buffer_transport", "host-memory");
    source_config.decode_mode =
        GetValue(sections, section, "decode_mode", "gstreamer-nv12-host");
    source_config.pixel_format = GetValue(sections, section, "pixel_format", "nv12");
    source_config.decode_timeout_seconds =
        GetIntValue(sections, section, "decode_timeout_seconds", 10);
    source_config.decode_log_path =
        "/tmp/evr_runtime_frontend_osd_multi_stream_" + stream_id + ".log";
    if (first_decode_mode.empty()) {
      first_decode_mode = source_config.decode_mode;
    }

    if (source_config.source_uri.empty()) {
      std::cerr << "stream " << stream_id << " has empty source_uri\n";
      return 1;
    }
    if (IsRtspUri(source_config.source_uri) && !HasJetsonDeviceRuntimePrerequisites()) {
      std::cerr << "skipping: Jetson RTSP hardware decode prerequisites are not available in "
                   "this session\n";
      return 77;
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
      const auto detections =
          detector.DetectImage(frame.bytes, frame.width, frame.height, frame.pixel_format, &error);
      assert(!detections.empty());
      assert(detections.front().class_name == "person");

      const auto rgb = FrameToRgb(frame);
      if (rgb.empty()) {
        std::cerr << "failed to convert frame to RGB for preview: pixel_format="
                  << frame.pixel_format << '\n';
        return 1;
      }
      const auto preview_path = PreviewPath(stream_dir, frame_id);
      const auto metadata_path = MetadataPath(stream_dir, frame_id);
      assert(WriteJpeg(preview_path, rgb, frame.width, frame.height, 80));
      assert(WriteFrontendOsdJson(metadata_path, stream_id, frame_id, source_config.decode_mode,
                                  frame, preview_path, detections));

      const std::string metadata = ReadTextFile(metadata_path);
      assert(metadata.find("\"osd_mode\": \"frontend-overlay\"") != std::string::npos);
      assert(metadata.find("\"boxes_burned_in\": false") != std::string::npos);
      assert(metadata.find("\"class_name\": \"person\"") != std::string::npos);
      assert(std::filesystem::exists(preview_path));
      ++artifacts_written;
    }

    source_session.Stop();
  }

  assert(stream_count >= 2U);
  assert(!first_decode_mode.empty());
  assert(artifacts_written == static_cast<int>(stream_count) * frame_count);
  std::cout << "ok: streams=" << stream_count << " frames_per_stream=" << frame_count
            << " preview_mode=frontend-osd"
            << " decode_mode=" << first_decode_mode
            << " output_dir=" << output_dir.string() << '\n';
  return 0;
#endif
}
