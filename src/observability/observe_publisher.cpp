#include "evr/runtime/observability/observe_publisher.h"

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace evr::runtime::observability {
namespace {

constexpr char kObserveSchemaVersion[] = "v1";
constexpr char kObserveProducerName[] = "edge-vision-runtime";
constexpr char kObserveProducerVersion[] = "0.1.0";

std::string EscapeJson(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8U);
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

void AppendTimingJson(std::ostringstream* out, const TimingBreakdown& timing) {
  *out << "\"timing\":{"
       << "\"decode_ms\":" << timing.decode_ms << ','
       << "\"preprocess_ms\":" << timing.preprocess_ms << ','
       << "\"infer_ms\":" << timing.infer_ms << ','
       << "\"postprocess_ms\":" << timing.postprocess_ms << ','
       << "\"e2e_ms\":" << timing.e2e_ms << '}';
}

void AppendDetectionJson(std::ostringstream* out, const DetectionBox& detection) {
  *out << '{'
       << "\"class_id\":" << detection.class_id << ','
       << "\"class_name\":\"" << EscapeJson(detection.class_name) << "\","
       << "\"score\":" << detection.score << ','
       << "\"box\":{"
       << "\"x\":" << detection.x << ','
       << "\"y\":" << detection.y << ','
       << "\"w\":" << detection.width << ','
       << "\"h\":" << detection.height << "}}";
}

void AppendEnvelopeJson(std::ostringstream* out, const char* type) {
  *out << "\"schema_version\":\"" << kObserveSchemaVersion << "\","
       << "\"producer\":{"
       << "\"name\":\"" << kObserveProducerName << "\","
       << "\"version\":\"" << kObserveProducerVersion << "\"},"
       << "\"type\":\"" << type << "\",";
}

}  // namespace

std::string EncodeStreamSnapshotNdjson(const StreamSnapshotEvent& event) {
  std::ostringstream out;
  out << '{';
  AppendEnvelopeJson(&out, "stream_snapshot");
  out
      << "\"stream_id\":\"" << EscapeJson(event.stream_id) << "\","
      << "\"source_session_id\":\"" << EscapeJson(event.source_session_id) << "\","
      << "\"decode_mode\":\"" << EscapeJson(event.decode_mode) << "\","
      << "\"pixel_format\":\"" << EscapeJson(event.pixel_format) << "\","
      << "\"buffer_transport\":\"" << EscapeJson(event.buffer_transport) << "\","
      << "\"preview_path\":\"" << EscapeJson(event.preview_path) << "\","
      << "\"metadata_path\":\"" << EscapeJson(event.metadata_path) << "\","
      << "\"input_fps\":" << event.input_fps << ','
      << "\"decoded_fps\":" << event.decoded_fps << ','
      << "\"infer_fps\":" << event.infer_fps << ','
      << "\"queue_depth\":" << event.queue_depth << ','
      << "\"drop_total\":" << event.drop_total << ','
      << "\"frame_age_ms\":" << event.frame_age_ms << ','
      << "\"last_detection_count\":" << event.last_detection_count << ','
      << "\"healthy\":" << (event.healthy ? "true" : "false") << ',';
  AppendTimingJson(&out, event.latency);
  out << "}\n";
  return out.str();
}

std::string EncodeFrontendOsdFrameNdjson(const FrontendOsdFrameEvent& event) {
  std::ostringstream out;
  out << '{';
  AppendEnvelopeJson(&out, "frontend_osd_frame");
  out
      << "\"source_session_id\":\"" << EscapeJson(event.source_session_id) << "\","
      << "\"frame_id\":" << event.frame_id << ','
      << "\"ts_ms\":" << event.ts_ms << ','
      << "\"decode_mode\":\"" << EscapeJson(event.decode_mode) << "\","
      << "\"pixel_format\":\"" << EscapeJson(event.pixel_format) << "\","
      << "\"buffer_transport\":\"" << EscapeJson(event.buffer_transport) << "\","
      << "\"preview\":{"
      << "\"path\":\"" << EscapeJson(event.preview_path) << "\","
      << "\"width\":" << event.preview_width << ','
      << "\"height\":" << event.preview_height << "},";
  AppendTimingJson(&out, event.timing);
  out << ",\"detections\":[";
  for (std::size_t index = 0; index < event.detections.size(); ++index) {
    if (index > 0U) {
      out << ',';
    }
    AppendDetectionJson(&out, event.detections[index]);
  }
  out << "]}\n";
  return out.str();
}

std::string EncodeRuntimeEventNdjson(const RuntimeEventRecord& event) {
  std::ostringstream out;
  out << '{';
  AppendEnvelopeJson(&out, "runtime_event");
  out
      << "\"level\":\"" << EscapeJson(event.level) << "\","
      << "\"stream_id\":\"" << EscapeJson(event.stream_id) << "\","
      << "\"event\":\"" << EscapeJson(event.event) << "\","
      << "\"message\":\"" << EscapeJson(event.message) << "\","
      << "\"ts_ms\":" << event.ts_ms << "}\n";
  return out.str();
}

ObservePublisher::ObservePublisher() = default;

ObservePublisher::~ObservePublisher() {
  Close();
}

bool ObservePublisher::Configure(const ObservePublisherConfig& config, std::string* error) {
  config_ = config;
  Close();
  if (!config_.enabled) {
    return true;
  }
  if (config_.socket_path.empty()) {
    if (error != nullptr) {
      *error = "observe publisher socket_path is empty";
    }
    return false;
  }
  sockaddr_un address{};
  if (config_.socket_path.size() >= sizeof(address.sun_path)) {
    if (error != nullptr) {
      *error = "observe publisher socket_path is too long";
    }
    return false;
  }
  return true;
}

bool ObservePublisher::connected() const {
  return socket_fd_ >= 0;
}

void ObservePublisher::Close() {
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
}

bool ObservePublisher::PublishStreamSnapshot(const StreamSnapshotEvent& event,
                                             std::string* error) {
  return PublishLine(EncodeStreamSnapshotNdjson(event), error);
}

bool ObservePublisher::PublishFrontendOsdFrame(const FrontendOsdFrameEvent& event,
                                               std::string* error) {
  return PublishLine(EncodeFrontendOsdFrameNdjson(event), error);
}

bool ObservePublisher::PublishRuntimeEvent(const RuntimeEventRecord& event,
                                           std::string* error) {
  return PublishLine(EncodeRuntimeEventNdjson(event), error);
}

bool ObservePublisher::Connect(std::string* error) {
  if (!config_.enabled) {
    return true;
  }
  if (socket_fd_ >= 0) {
    return true;
  }

  socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd_ < 0) {
    if (error != nullptr) {
      *error = "failed to create observe publisher socket: " + std::string(std::strerror(errno));
    }
    return false;
  }

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", config_.socket_path.c_str());
  if (connect(socket_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    if (error != nullptr) {
      *error = "failed to connect observe publisher socket " + config_.socket_path + ": " +
               std::string(std::strerror(errno));
    }
    Close();
    return false;
  }
  return true;
}

bool ObservePublisher::PublishLine(const std::string& line, std::string* error) {
  if (!config_.enabled) {
    return true;
  }
  if (!Connect(error)) {
    return false;
  }

  std::size_t offset = 0U;
  while (offset < line.size()) {
    const ssize_t written =
        write(socket_fd_, line.data() + offset, line.size() - offset);
    if (written < 0) {
      if (error != nullptr) {
        *error = "failed to write observe publisher message: " +
                 std::string(std::strerror(errno));
      }
      Close();
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

}  // namespace evr::runtime::observability
