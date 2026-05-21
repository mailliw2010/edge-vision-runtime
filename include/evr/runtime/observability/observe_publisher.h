#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace evr::runtime::observability {

struct DetectionBox {
  int class_id{0};
  std::string class_name;
  float score{0.0F};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

struct TimingBreakdown {
  int decode_ms{0};
  int preprocess_ms{0};
  int infer_ms{0};
  int postprocess_ms{0};
  int e2e_ms{0};
};

struct StreamSnapshotEvent {
  std::string stream_id;
  std::string source_session_id;
  std::string decode_mode;
  std::string pixel_format;
  std::string buffer_transport;
  std::string preview_path;
  std::string metadata_path;
  double input_fps{0.0};
  double decoded_fps{0.0};
  double infer_fps{0.0};
  int queue_depth{0};
  std::uint64_t drop_total{0};
  int frame_age_ms{0};
  int last_detection_count{0};
  bool healthy{true};
  TimingBreakdown latency{};
};

struct FrontendOsdFrameEvent {
  std::string source_session_id;
  std::uint64_t frame_id{0};
  std::int64_t ts_ms{0};
  std::string decode_mode;
  std::string pixel_format;
  std::string buffer_transport;
  std::string preview_path;
  int preview_width{0};
  int preview_height{0};
  TimingBreakdown timing{};
  std::vector<DetectionBox> detections;
};

struct RuntimeEventRecord {
  std::string level;
  std::string stream_id;
  std::string event;
  std::string message;
  std::int64_t ts_ms{0};
};

struct ObservePublisherConfig {
  bool enabled{false};
  std::string socket_path;
};

std::string EncodeStreamSnapshotNdjson(const StreamSnapshotEvent& event);
std::string EncodeFrontendOsdFrameNdjson(const FrontendOsdFrameEvent& event);
std::string EncodeRuntimeEventNdjson(const RuntimeEventRecord& event);

class ObservePublisher {
 public:
  ObservePublisher();
  ~ObservePublisher();

  ObservePublisher(const ObservePublisher&) = delete;
  ObservePublisher& operator=(const ObservePublisher&) = delete;

  bool Configure(const ObservePublisherConfig& config, std::string* error);
  bool connected() const;
  void Close();

  bool PublishStreamSnapshot(const StreamSnapshotEvent& event, std::string* error);
  bool PublishFrontendOsdFrame(const FrontendOsdFrameEvent& event, std::string* error);
  bool PublishRuntimeEvent(const RuntimeEventRecord& event, std::string* error);

 private:
  bool Connect(std::string* error);
  bool PublishLine(const std::string& line, std::string* error);

  ObservePublisherConfig config_{};
  int socket_fd_{-1};
};

}  // namespace evr::runtime::observability
