#pragma once

#include <string>
#include <string_view>

#include "evr/runtime/session/lifecycle_session.h"

namespace evr::runtime::supervisor {

struct SupervisorSessionConfig {
  std::string session_id{"supervisor-main"};
  std::string control_endpoint{"unix:///tmp/evr-supervisor.sock"};
  std::string proto_version{"v1"};
};

class SupervisorSession final : public session::LifecycleSession {
 public:
  bool Configure(const SupervisorSessionConfig& config);

  std::string_view Kind() const override;
  session::State state() const override;
  bool Start() override;
  void Stop() override;
  session::Snapshot GetSnapshot() const override;

 private:
  SupervisorSessionConfig config_{};
  session::State state_{session::State::kCreated};
};

}  // namespace evr::runtime::supervisor
