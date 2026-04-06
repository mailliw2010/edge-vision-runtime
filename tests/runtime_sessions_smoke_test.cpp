#include <cassert>

#include "evr/runtime/source/source_session.h"
#include "evr/runtime/supervisor/supervisor_session.h"
#include "evr/runtime/worker/worker_session.h"

int main() {
  using evr::runtime::session::State;

  evr::runtime::supervisor::SupervisorSession supervisor_session;
  evr::runtime::supervisor::SupervisorSessionConfig supervisor_config;
  supervisor_config.session_id = "supervisor-test";
  supervisor_config.control_endpoint = "unix:///tmp/test-supervisor.sock";
  supervisor_config.proto_version = "v1";
  assert(supervisor_session.Configure(supervisor_config));
  assert(supervisor_session.Start());
  assert(supervisor_session.GetSnapshot().state == State::kRunning);
  supervisor_session.Stop();
  assert(supervisor_session.state() == State::kStopped);

  evr::runtime::source::SourceSession source_session;
  evr::runtime::source::SourceSessionConfig source_config;
  source_config.session_id = "source-test";
  source_config.source_uri = "rtsp://example.local/test";
  source_config.proto_version = "v1";
  source_config.decode_mode = "jetson-nvdec";
  assert(source_session.Configure(source_config));
  assert(source_session.Start());
  assert(source_session.GetSnapshot().state == State::kRunning);
  source_session.Stop();
  assert(source_session.state() == State::kStopped);

  evr::runtime::worker::WorkerSession worker_session;
  evr::runtime::worker::WorkerSessionConfig worker_config;
  worker_config.session_id = "worker-test";
  worker_config.supervisor_endpoint = "unix:///tmp/test-supervisor.sock";
  worker_config.source_session_id = "source-test";
  worker_config.proto_version = "v1";
  worker_config.inference_backend = "tensorrt";
  worker_config.engine_path = "models/test.plan";
  assert(worker_session.Configure(worker_config));
  assert(worker_session.Start());
  assert(worker_session.GetSnapshot().state == State::kRunning);
  worker_session.Stop();
  assert(worker_session.state() == State::kStopped);

  return 0;
}
