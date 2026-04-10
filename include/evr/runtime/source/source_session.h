#pragma once

#include <string>
#include <string_view>

#include "evr/runtime/session/lifecycle_session.h"

namespace evr::runtime::source {

struct SourceSessionConfig {
  std::string session_id{"source-demo"};
  std::string source_uri{"rtsp://example.local/camera-0"};
  std::string upstream_kind{"direct-rtsp"};
  std::string upstream_endpoint{""};
  std::string proto_version{"v1"};
  std::string decode_mode{"jetson-nvdec"};
  std::string pixel_format{"nv12"};
};

class SourceSession final : public session::LifecycleSession {
 public:
  bool Configure(const SourceSessionConfig& config);

  std::string_view Kind() const override;
  session::State state() const override;
  bool Start() override;
  void Stop() override;
  session::Snapshot GetSnapshot() const override;

 private:
  SourceSessionConfig config_{};
  session::State state_{session::State::kCreated};
};

}  // namespace evr::runtime::source
