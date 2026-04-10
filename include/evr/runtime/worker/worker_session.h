#pragma once

#include <string>
#include <string_view>

#include "evr/runtime/session/lifecycle_session.h"

namespace evr::runtime::worker {

struct WorkerSessionConfig {
  std::string session_id{"worker-0"};
  std::string supervisor_endpoint{"unix:///tmp/evr-supervisor.sock"};
  std::string source_session_id{"source-demo"};
  std::string proto_version{"v1"};
  std::string inference_backend{"tensorrt"};
  std::string engine_path{"models/detector.plan"};
  std::string algorithm_name{"detector"};
  std::string input_binding{"frames"};
  std::string output_topic{"events.detection"};
};

class WorkerSession final : public session::LifecycleSession {
 public:
  bool Configure(const WorkerSessionConfig& config);

  std::string_view Kind() const override;
  session::State state() const override;
  bool Start() override;
  void Stop() override;
  session::Snapshot GetSnapshot() const override;

 private:
  WorkerSessionConfig config_{};
  session::State state_{session::State::kCreated};
};

}  // namespace evr::runtime::worker
