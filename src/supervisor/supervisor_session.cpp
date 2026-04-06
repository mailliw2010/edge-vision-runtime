#include "evr/runtime/supervisor/supervisor_session.h"

namespace evr::runtime::supervisor {

bool SupervisorSession::Configure(const SupervisorSessionConfig& config) {
  if (state_ == session::State::kRunning) {
    return false;
  }

  config_ = config;
  state_ = session::State::kConfigured;
  return true;
}

std::string_view SupervisorSession::Kind() const { return "supervisor"; }

session::State SupervisorSession::state() const { return state_; }

bool SupervisorSession::Start() {
  if (state_ != session::State::kConfigured) {
    return false;
  }

  state_ = session::State::kRunning;
  return true;
}

void SupervisorSession::Stop() {
  if (state_ == session::State::kRunning) {
    state_ = session::State::kStopped;
  }
}

session::Snapshot SupervisorSession::GetSnapshot() const {
  session::Snapshot snapshot;
  snapshot.kind = std::string(Kind());
  snapshot.session_id = config_.session_id;
  snapshot.state = state_;
  snapshot.detail = "endpoint=" + config_.control_endpoint + ", proto=" + config_.proto_version;
  return snapshot;
}

}  // namespace evr::runtime::supervisor
