#pragma once

#include <string>

#include "evr/runtime/session/session_state.h"
#include "evr/runtime/source/source_session.h"
#include "evr/runtime/supervisor/supervisor_session.h"
#include "evr/runtime/worker/worker_session.h"

namespace evr::runtime::deployment {

struct Phase1DeploymentSpec {
  std::string deployment_id{"phase1-demo"};
  supervisor::SupervisorSessionConfig supervisor{};
  source::SourceSessionConfig source{};
  worker::WorkerSessionConfig worker{};

  bool Normalize(std::string* error = nullptr);
  std::string DescribeWiring() const;
};

struct DeploymentStatus {
  std::string deployment_id;
  std::string wiring;
  session::State state{session::State::kCreated};
  std::string detail;
};

class DeploymentController {
 public:
  bool Apply(Phase1DeploymentSpec spec, std::string* error = nullptr);
  DeploymentStatus GetStatus() const;
  const Phase1DeploymentSpec* spec() const;

 private:
  bool applied_{false};
  Phase1DeploymentSpec spec_{};
  DeploymentStatus status_{};
};

}  // namespace evr::runtime::deployment
