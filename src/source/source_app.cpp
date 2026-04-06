#include "evr/runtime/source/source_app.h"

#include <iostream>
#include <utility>

#include "evr/runtime/session/session_state.h"

namespace evr::runtime::source {
namespace {

void PrintSnapshot(const session::Snapshot& snapshot) {
  std::cout << snapshot.kind << "[" << snapshot.session_id << "] "
            << session::ToString(snapshot.state) << " - " << snapshot.detail << '\n';
}

}  // namespace

SourceApp::SourceApp(SourceAppConfig config) : config_(std::move(config)) {}

int SourceApp::Run() {
  SourceSession source_session;
  if (!source_session.Configure(config_.session)) {
    std::cerr << "failed to configure source session" << std::endl;
    return 1;
  }

  if (!source_session.Start()) {
    std::cerr << "failed to start source session" << std::endl;
    return 1;
  }

  PrintSnapshot(source_session.GetSnapshot());
  source_session.Stop();
  return 0;
}

}  // namespace evr::runtime::source
