#include "evr/runtime/worker/worker_app.h"

#include <iostream>
#include <utility>

#include "evr/runtime/session/session_state.h"

namespace evr::runtime::worker {
namespace {

void PrintSnapshot(const session::Snapshot& snapshot) {
  std::cout << snapshot.kind << "[" << snapshot.session_id << "] "
            << session::ToString(snapshot.state) << " - " << snapshot.detail << '\n';
}

}  // namespace

WorkerApp::WorkerApp(WorkerAppConfig config) : config_(std::move(config)) {}

int WorkerApp::Run() {
  source::SourceSession source_session;
  WorkerSession worker_session;

  if (!source_session.Configure(config_.source)) {
    std::cerr << "failed to configure source session" << std::endl;
    return 1;
  }

  if (!worker_session.Configure(config_.worker)) {
    std::cerr << "failed to configure worker session" << std::endl;
    return 1;
  }

  if (!source_session.Start()) {
    std::cerr << "failed to start source session" << std::endl;
    return 1;
  }

  if (!worker_session.Start()) {
    std::cerr << "failed to start worker session" << std::endl;
    return 1;
  }

  PrintSnapshot(source_session.GetSnapshot());
  PrintSnapshot(worker_session.GetSnapshot());

  worker_session.Stop();
  source_session.Stop();
  return 0;
}

}  // namespace evr::runtime::worker
