#include "evr/runtime/source/source_session.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>

#if defined(EVR_WITH_GSTREAMER)
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#endif

namespace evr::runtime::source {
namespace {

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

bool IsRtspUri(const std::string& uri) {
  return uri.rfind("rtsp://", 0) == 0 || uri.rfind("rtsps://", 0) == 0;
}

bool IsGStreamerTestSourceUri(const std::string& uri) {
  return uri.rfind("gst-testsrc://", 0) == 0;
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool IsGStreamerDecodeMode(const std::string& decode_mode) {
  const std::string normalized = ToLower(decode_mode);
  return normalized == "gstreamer" || normalized == "gst" ||
         normalized == "gstreamer-appsink" || normalized == "gst-appsink";
}

std::vector<std::uint8_t> ReadCommandBytes(const std::string& command, int* exit_status) {
  std::vector<std::uint8_t> bytes;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    if (exit_status != nullptr) {
      *exit_status = -1;
    }
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

  const int status = pclose(pipe);
  if (exit_status != nullptr) {
    *exit_status = status;
  }
  if (status != 0) {
    bytes.clear();
  }
  return bytes;
}

#if defined(EVR_WITH_GSTREAMER)
class GstObjectPtr {
 public:
  explicit GstObjectPtr(GstObject* object = nullptr) : object_(object) {}
  ~GstObjectPtr() {
    if (object_ != nullptr) {
      gst_object_unref(object_);
    }
  }
  GstObjectPtr(const GstObjectPtr&) = delete;
  GstObjectPtr& operator=(const GstObjectPtr&) = delete;
  GstObject* get() const { return object_; }

 private:
  GstObject* object_{nullptr};
};

class GstSamplePtr {
 public:
  explicit GstSamplePtr(GstSample* sample = nullptr) : sample_(sample) {}
  ~GstSamplePtr() {
    if (sample_ != nullptr) {
      gst_sample_unref(sample_);
    }
  }
  GstSamplePtr(const GstSamplePtr&) = delete;
  GstSamplePtr& operator=(const GstSamplePtr&) = delete;
  GstSample* get() const { return sample_; }

 private:
  GstSample* sample_{nullptr};
};

std::string GstEscape(const std::string& value) {
  std::string escaped = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      escaped += "\\'";
    } else {
      escaped += ch;
    }
  }
  escaped += "'";
  return escaped;
}

std::vector<FrameBuffer> CaptureFramesWithGStreamer(const SourceSessionConfig& config,
                                                    int width,
                                                    int height,
                                                    int frame_count,
                                                    std::string* error) {
  static bool initialized = false;
  if (!initialized) {
    gst_init(nullptr, nullptr);
    initialized = true;
  }

  std::string pipeline_description;
  if (IsGStreamerTestSourceUri(config.source_uri)) {
    pipeline_description =
        "videotestsrc is-live=false num-buffers=" + std::to_string(frame_count) +
        " pattern=smpte ! videoconvert ! videoscale ! video/x-raw,format=RGBA,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        " ! appsink name=evr_sink sync=false max-buffers=" + std::to_string(frame_count) +
        " drop=false";
  } else {
    pipeline_description =
        "uridecodebin uri=" + GstEscape(config.source_uri) +
        " ! videoconvert ! videoscale ! video/x-raw,format=RGBA,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        " ! appsink name=evr_sink sync=false max-buffers=" + std::to_string(frame_count) +
        " drop=false";
  }

  GError* parse_error = nullptr;
  GstElement* pipeline = gst_parse_launch(pipeline_description.c_str(), &parse_error);
  if (pipeline == nullptr) {
    if (error != nullptr) {
      *error = "failed to create GStreamer pipeline";
      if (parse_error != nullptr && parse_error->message != nullptr) {
        *error += ": ";
        *error += parse_error->message;
      }
    }
    if (parse_error != nullptr) {
      g_error_free(parse_error);
    }
    return {};
  }
  if (parse_error != nullptr) {
    g_error_free(parse_error);
  }
  GstObjectPtr pipeline_owner(GST_OBJECT(pipeline));

  GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "evr_sink");
  if (sink == nullptr) {
    if (error != nullptr) {
      *error = "failed to find GStreamer appsink";
    }
    return {};
  }
  GstObjectPtr sink_owner(GST_OBJECT(sink));

  const GstStateChangeReturn state_result = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  if (state_result == GST_STATE_CHANGE_FAILURE) {
    if (error != nullptr) {
      *error = "failed to start GStreamer pipeline for " + RedactUri(config.source_uri);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    return {};
  }

  std::vector<FrameBuffer> frames;
  frames.reserve(static_cast<std::size_t>(frame_count));
  const auto timeout_ns =
      static_cast<GstClockTime>(std::max(config.decode_timeout_seconds, 1)) * GST_SECOND;
  const std::size_t expected_bytes =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;

  for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
    GstSamplePtr sample(gst_app_sink_try_pull_sample(GST_APP_SINK(sink), timeout_ns));
    if (sample.get() == nullptr) {
      if (error != nullptr) {
        *error = "timed out pulling GStreamer frame " + std::to_string(frame_id) + " from " +
                 RedactUri(config.source_uri);
      }
      gst_element_set_state(pipeline, GST_STATE_NULL);
      return {};
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample.get());
    GstMapInfo map_info{};
    if (buffer == nullptr || gst_buffer_map(buffer, &map_info, GST_MAP_READ) == FALSE) {
      if (error != nullptr) {
        *error = "failed to map GStreamer frame buffer";
      }
      gst_element_set_state(pipeline, GST_STATE_NULL);
      return {};
    }
    if (map_info.size < expected_bytes) {
      if (error != nullptr) {
        *error = "GStreamer frame is smaller than expected: expected " +
                 std::to_string(expected_bytes) + " bytes, got " +
                 std::to_string(map_info.size);
      }
      gst_buffer_unmap(buffer, &map_info);
      gst_element_set_state(pipeline, GST_STATE_NULL);
      return {};
    }

    FrameBuffer frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(map_info.data, map_info.data + expected_bytes);
    gst_buffer_unmap(buffer, &map_info);
    frames.push_back(std::move(frame));
  }

  gst_element_set_state(pipeline, GST_STATE_NULL);
  return frames;
}
#endif

}  // namespace

bool SourceSession::Configure(const SourceSessionConfig& config) {
  if (state_ == session::State::kRunning) {
    return false;
  }

  config_ = config;
  state_ = session::State::kConfigured;
  return true;
}

std::string_view SourceSession::Kind() const { return "source"; }

session::State SourceSession::state() const { return state_; }

bool SourceSession::Start() {
  if (state_ != session::State::kConfigured) {
    return false;
  }

  state_ = session::State::kRunning;
  return true;
}

void SourceSession::Stop() {
  if (state_ == session::State::kRunning) {
    state_ = session::State::kStopped;
  }
}

session::Snapshot SourceSession::GetSnapshot() const {
  session::Snapshot snapshot;
  snapshot.kind = std::string(Kind());
  snapshot.session_id = config_.session_id;
  snapshot.state = state_;
  snapshot.detail = "uri=" + RedactUri(config_.source_uri) +
                    ", upstream_kind=" + config_.upstream_kind +
                    ", upstream_endpoint=" + RedactUri(config_.upstream_endpoint) +
                    ", transport=" + config_.transport_protocol +
                    ", buffer=" + config_.buffer_transport + ", proto=" + config_.proto_version +
                    ", decode=" + config_.decode_mode + ", pixel=" + config_.pixel_format;
  return snapshot;
}

FrameBuffer SourceSession::MakeSyntheticFrame(int width, int height) const {
  FrameBuffer frame;
  frame.width = width;
  frame.height = height;
  frame.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 127U);
  return frame;
}

std::vector<FrameBuffer> SourceSession::CaptureFramesFromSource(int width,
                                                                int height,
                                                                int frame_count,
                                                                std::string* error) const {
  if (width <= 0 || height <= 0 || frame_count <= 0) {
    if (error != nullptr) {
      *error = "invalid source capture shape";
    }
    return {};
  }
  if (config_.source_uri.empty()) {
    if (error != nullptr) {
      *error = "source uri is empty";
    }
    return {};
  }

  if (IsGStreamerDecodeMode(config_.decode_mode)) {
#if defined(EVR_WITH_GSTREAMER)
    return CaptureFramesWithGStreamer(config_, width, height, frame_count, error);
#else
    if (error != nullptr) {
      *error = "GStreamer backend requested, but GStreamer was not found at build time";
    }
    return {};
#endif
  }

  std::string command;
  if (config_.decode_timeout_seconds > 0) {
    command += "timeout " + std::to_string(config_.decode_timeout_seconds) + "s ";
  }
  command += "ffmpeg -nostdin -hide_banner -loglevel warning ";
  if (IsRtspUri(config_.source_uri)) {
    command += "-rtsp_transport tcp -stimeout 10000000 ";
  }
  command += "-i " + ShellQuote(config_.source_uri) + " -an -frames:v " +
             std::to_string(frame_count) + " -vf scale=" + std::to_string(width) + ":" +
             std::to_string(height) + " -f rawvideo -pix_fmt rgba - "
             "2>" +
             ShellQuote(config_.decode_log_path);

  int decode_exit_status = 0;
  const auto raw_video = ReadCommandBytes(command, &decode_exit_status);
  const std::size_t frame_bytes =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  const std::size_t expected_bytes = frame_bytes * static_cast<std::size_t>(frame_count);
  if (raw_video.size() != expected_bytes) {
    if (error != nullptr) {
      *error = "failed to decode " + std::to_string(frame_count) + " frame(s) from " +
               RedactUri(config_.source_uri) + ": expected " + std::to_string(expected_bytes) +
               " bytes, got " + std::to_string(raw_video.size()) + "; ffmpeg exit status " +
               std::to_string(decode_exit_status) + "; see " + config_.decode_log_path;
    }
    return {};
  }

  std::vector<FrameBuffer> frames;
  frames.reserve(static_cast<std::size_t>(frame_count));
  for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
    const auto first = raw_video.begin() + static_cast<std::ptrdiff_t>(frame_bytes * frame_id);
    const auto last = first + static_cast<std::ptrdiff_t>(frame_bytes);
    FrameBuffer frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(first, last);
    frames.push_back(std::move(frame));
  }
  return frames;
}

}  // namespace evr::runtime::source
