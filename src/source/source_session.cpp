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

#if defined(EVR_WITH_GSTREAMER)
bool HasElementFactory(const char* name) {
  GstElementFactory* factory = gst_element_factory_find(name);
  if (factory == nullptr) {
    return false;
  }
  gst_object_unref(factory);
  return true;
}

bool HasSoftwareH264Decoder() {
  return HasElementFactory("avdec_h264");
}

bool HasJetsonHardwareDecodePath() {
  return HasElementFactory("nvv4l2decoder") && HasElementFactory("nvvidconv");
}
#endif

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

class GstMessagePtr {
 public:
  explicit GstMessagePtr(GstMessage* message = nullptr) : message_(message) {}
  ~GstMessagePtr() {
    if (message_ != nullptr) {
      gst_message_unref(message_);
    }
  }
  GstMessagePtr(const GstMessagePtr&) = delete;
  GstMessagePtr& operator=(const GstMessagePtr&) = delete;
  GstMessage* get() const { return message_; }

 private:
  GstMessage* message_{nullptr};
};


std::string ReadGStreamerBusError(GstElement* pipeline, GstClockTime timeout = 0) {
  if (pipeline == nullptr) {
    return {};
  }

  GstBus* bus = gst_element_get_bus(pipeline);
  if (bus == nullptr) {
    return {};
  }
  GstObjectPtr bus_owner(GST_OBJECT(bus));

  GstMessagePtr message(gst_bus_timed_pop_filtered(
      bus, timeout, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING |
                                                GST_MESSAGE_EOS)));
  if (message.get() == nullptr) {
    return {};
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_ERROR) {
    GError* gst_error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_error(message.get(), &gst_error, &debug);
    std::string detail = "GStreamer error";
    if (GST_MESSAGE_SRC(message.get()) != nullptr) {
      detail += " from ";
      detail += GST_OBJECT_NAME(GST_MESSAGE_SRC(message.get()));
    }
    if (gst_error != nullptr && gst_error->message != nullptr) {
      detail += ": ";
      detail += gst_error->message;
    }
    if (debug != nullptr && debug[0] != '\0') {
      detail += " [debug: ";
      detail += debug;
      detail += "]";
    }
    if (gst_error != nullptr) {
      g_error_free(gst_error);
    }
    if (debug != nullptr) {
      g_free(debug);
    }
    return detail;
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_WARNING) {
    GError* gst_error = nullptr;
    gchar* debug = nullptr;
    gst_message_parse_warning(message.get(), &gst_error, &debug);
    std::string detail = "GStreamer warning";
    if (GST_MESSAGE_SRC(message.get()) != nullptr) {
      detail += " from ";
      detail += GST_OBJECT_NAME(GST_MESSAGE_SRC(message.get()));
    }
    if (gst_error != nullptr && gst_error->message != nullptr) {
      detail += ": ";
      detail += gst_error->message;
    }
    if (debug != nullptr && debug[0] != '\0') {
      detail += " [debug: ";
      detail += debug;
      detail += "]";
    }
    if (gst_error != nullptr) {
      g_error_free(gst_error);
    }
    if (debug != nullptr) {
      g_free(debug);
    }
    return detail;
  }

  if (GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_EOS) {
    return "GStreamer reached end-of-stream before enough frames were decoded";
  }

  return {};
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

  // Build pipeline. For gstreamer-testsrc, use a fully static gst_parse_launch pipeline.
  // For RTSP on Jetson, prefer the explicit hardware path that we validate separately with
  // runtime_gstreamer_hw_decode_probe. For everything else, keep a more generic playbin path.
  GstElement* pipeline = nullptr;
  GstElement* sink = nullptr;

  if (config.upstream_kind == "gstreamer-testsrc" ||
      IsGStreamerTestSourceUri(config.source_uri)) {
    const std::string pipeline_description =
        "videotestsrc is-live=false num-buffers=" + std::to_string(frame_count) +
        " pattern=smpte ! videoconvert ! videoscale ! video/x-raw,format=RGBA,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        " ! appsink name=evr_sink sync=false max-buffers=" + std::to_string(frame_count) +
        " drop=false";
    GError* parse_error = nullptr;
    pipeline = gst_parse_launch(pipeline_description.c_str(), &parse_error);
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
    sink = gst_bin_get_by_name(GST_BIN(pipeline), "evr_sink");
  } else if (IsRtspUri(config.source_uri) && HasJetsonHardwareDecodePath()) {
    const std::string pipeline_description =
        "rtspsrc location=\"" + config.source_uri +
        "\" protocols=tcp latency=200 ! rtph264depay ! h264parse ! "
        "nvv4l2decoder ! nvvidconv ! video/x-raw,format=RGBA,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        " ! appsink name=evr_sink sync=false max-buffers=" + std::to_string(frame_count) +
        " drop=false";
    GError* parse_error = nullptr;
    pipeline = gst_parse_launch(pipeline_description.c_str(), &parse_error);
    if (pipeline == nullptr) {
      if (error != nullptr) {
        *error = "failed to create Jetson RTSP GStreamer pipeline";
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
    sink = gst_bin_get_by_name(GST_BIN(pipeline), "evr_sink");
  } else {
    if (IsRtspUri(config.source_uri) && !HasSoftwareH264Decoder()) {
      if (error != nullptr) {
        *error = "GStreamer RTSP decode requires either the Jetson hardware decode path "
                 "(nvv4l2decoder + nvvidconv) or a software H.264 decoder (avdec_h264); "
                 "neither is available";
      }
      return {};
    }
    pipeline = gst_element_factory_make("playbin", nullptr);
    if (pipeline == nullptr) {
      if (error != nullptr) {
        *error = "failed to create GStreamer playbin element";
      }
      return {};
    }
    g_object_set(pipeline, "uri", config.source_uri.c_str(), nullptr);
    g_object_set(pipeline, "message-forward", TRUE, nullptr);

    const std::string sink_desc =
        "videoconvert ! videoscale ! video/x-raw,format=RGBA,width=" +
        std::to_string(width) + ",height=" + std::to_string(height) +
        " ! appsink name=evr_sink sync=false max-buffers=" + std::to_string(frame_count) +
        " drop=false";
    GError* sink_err = nullptr;
    GstElement* video_sink_bin =
        gst_parse_bin_from_description(sink_desc.c_str(), TRUE, &sink_err);
    if (video_sink_bin == nullptr) {
      if (error != nullptr) {
        *error = "failed to create GStreamer video sink";
        if (sink_err != nullptr && sink_err->message != nullptr) {
          *error += ": ";
          *error += sink_err->message;
        }
      }
      if (sink_err != nullptr) {
        g_error_free(sink_err);
      }
      gst_object_unref(pipeline);
      return {};
    }
    if (sink_err != nullptr) {
      g_error_free(sink_err);
    }
    // Fetch the appsink reference from video_sink_bin before transferring ownership
    // to playbin: once playbin owns the bin, gst_bin_get_by_name on the playbin
    // element will not traverse into it while the pipeline is in the NULL state.
    sink = gst_bin_get_by_name(GST_BIN(video_sink_bin), "evr_sink");
    g_object_set(pipeline, "video-sink", video_sink_bin, nullptr);
    gst_object_unref(video_sink_bin);
  }

  GstObjectPtr pipeline_owner(GST_OBJECT(pipeline));

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
      const std::string bus_error = ReadGStreamerBusError(pipeline, 500 * GST_MSECOND);
      if (!bus_error.empty()) {
        *error += ": " + bus_error;
      }
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    return {};
  }
  if (state_result == GST_STATE_CHANGE_ASYNC) {
    GstState current_state = GST_STATE_NULL;
    GstState pending_state = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn wait_result =
        gst_element_get_state(pipeline, &current_state, &pending_state, 5 * GST_SECOND);
    if (wait_result == GST_STATE_CHANGE_FAILURE || current_state != GST_STATE_PLAYING) {
      if (error != nullptr) {
        *error = "GStreamer pipeline did not reach PLAYING for " + RedactUri(config.source_uri);
        const std::string bus_error = ReadGStreamerBusError(pipeline, 500 * GST_MSECOND);
        if (!bus_error.empty()) {
          *error += ": " + bus_error;
        }
      }
      gst_element_set_state(pipeline, GST_STATE_NULL);
      return {};
    }
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
        const std::string bus_error = ReadGStreamerBusError(pipeline);
        if (!bus_error.empty()) {
          *error += ": " + bus_error;
        }
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
