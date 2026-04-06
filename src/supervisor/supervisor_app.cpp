#include "evr/runtime/supervisor/supervisor_app.h"

#include <iostream>
#include <utility>

#include "evr/runtime/session/session_state.h"

namespace evr::runtime::supervisor {
namespace {

void PrintSnapshot(const session::Snapshot& snapshot) {
  std::cout << snapshot.kind << "[" << snapshot.session_id << "] "
            << session::ToString(snapshot.state) << " - " << snapshot.detail << '\n';
}

}  // namespace

SupervisorApp::SupervisorApp(SupervisorAppConfig config) : config_(std::move(config)) {}

int SupervisorApp::Run() {
  SupervisorSession supervisor_session;
  if (!supervisor_session.Configure(config_.session)) {
    std::cerr << "failed to configure supervisor session" << std::endl;
    return 1;
  }

  if (!supervisor_session.Start()) {
    std::cerr << "failed to start supervisor session" << std::endl;
    return 1;
  }

  PrintSnapshot(supervisor_session.GetSnapshot());
  supervisor_session.Stop();
  return 0;
}

}  // namespace evr::runtime::supervisor
