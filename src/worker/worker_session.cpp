#include "evr/runtime/worker/worker_session.h"

namespace evr::runtime::worker {

bool WorkerSession::Configure(const WorkerSessionConfig& config) {
  if (state_ == session::State::kRunning) {
    return false;
  }

  config_ = config;
  state_ = session::State::kConfigured;
  return true;
}

std::string_view WorkerSession::Kind() const { return "worker"; }

session::State WorkerSession::state() const { return state_; }

bool WorkerSession::Start() {
  if (state_ != session::State::kConfigured) {
    return false;
  }

  state_ = session::State::kRunning;
  return true;
}

void WorkerSession::Stop() {
  if (state_ == session::State::kRunning) {
    state_ = session::State::kStopped;
  }
}

session::Snapshot WorkerSession::GetSnapshot() const {
  session::Snapshot snapshot;
  snapshot.kind = std::string(Kind());
  snapshot.session_id = config_.session_id;
  snapshot.state = state_;
  snapshot.detail = "source=" + config_.source_session_id + ", supervisor=" +
                    config_.supervisor_endpoint + ", proto=" + config_.proto_version +
                    ", backend=" + config_.inference_backend + ", engine=" +
                    config_.engine_path + ", algorithm=" + config_.algorithm_name +
                    ", package=" + config_.algorithm_package_uri + ", entry=" +
                    config_.algorithm_entry_point + ", runtime_config=" +
                    config_.algorithm_runtime_config_uri + ", input=" + config_.input_binding +
                    ", encoding=" + config_.result_encoding + ", output=" + config_.output_topic +
                    ", model=" + config_.engine_path;
  return snapshot;
}

}  // namespace evr::runtime::worker
