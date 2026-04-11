#include "evr/runtime/source/source_session.h"

namespace evr::runtime::source {

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
  snapshot.detail = "uri=" + config_.source_uri + ", upstream_kind=" + config_.upstream_kind +
                    ", upstream_endpoint=" + config_.upstream_endpoint + ", transport=" +
                    config_.transport_protocol + ", buffer=" + config_.buffer_transport +
                    ", proto=" + config_.proto_version + ", decode=" + config_.decode_mode +
                    ", pixel=" + config_.pixel_format;
  return snapshot;
}

}  // namespace evr::runtime::source
