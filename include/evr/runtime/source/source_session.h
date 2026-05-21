#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "evr/runtime/session/lifecycle_session.h"

namespace evr::runtime::source {

struct SourceSessionConfig {
  std::string session_id{"source-demo"};
  std::string source_uri{"rtsp://example.local/camera-0"};
  std::string upstream_kind{"direct-rtsp"};
  std::string upstream_endpoint{""};
  std::string transport_protocol{"rtsp"};
  std::string buffer_transport{"nvmm"};
  std::string proto_version{"v1"};
  std::string decode_mode{"jetson-nvdec"};
  std::string pixel_format{"nv12"};
  int decode_timeout_seconds{60};
  std::string decode_log_path{"/tmp/evr_runtime_source_ffmpeg.log"};
};

struct FrameBuffer {
  int width{0};
  int height{0};
  std::string pixel_format{"rgba"};
  std::string buffer_transport{"host-memory"};
  std::vector<std::uint8_t> bytes;
};

class SourceSession final : public session::LifecycleSession {
 public:
  bool Configure(const SourceSessionConfig& config);

  std::string_view Kind() const override;
  session::State state() const override;
  bool Start() override;
  void Stop() override;
  session::Snapshot GetSnapshot() const override;

  FrameBuffer MakeSyntheticFrame(int width, int height) const;
  std::vector<FrameBuffer> CaptureFramesFromSource(int width,
                                                   int height,
                                                   int frame_count,
                                                   std::string* error) const;

 private:
  SourceSessionConfig config_{};
  session::State state_{session::State::kCreated};
};

}  // namespace evr::runtime::source
