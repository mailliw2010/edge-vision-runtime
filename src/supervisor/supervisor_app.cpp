#include "evr/runtime/supervisor/supervisor_app.h"

#include <iostream>
#include <utility>

#include "evr/runtime/deployment/phase1_deployment.h"
#include "evr/runtime/session/session_state.h"

namespace evr::runtime::supervisor {
namespace {

void PrintSnapshot(const session::Snapshot& snapshot) {
  std::cout << snapshot.kind << "[" << snapshot.session_id << "] "
            << session::ToString(snapshot.state) << " - " << snapshot.detail << '\n';
}

void PrintDeploymentStatus(const deployment::DeploymentStatus& status) {
  std::cout << "deployment[" << status.deployment_id << "] "
            << session::ToString(status.state) << " - " << status.wiring;

  if (!status.detail.empty()) {
    std::cout << " (" << status.detail << ")";
  }

  std::cout << '\n';
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

  deployment::DeploymentController deployment_controller;
  auto deployment_spec = config_.deployment;
  deployment_spec.supervisor = config_.session;

  std::string deployment_error;
  if (!deployment_controller.Apply(std::move(deployment_spec), &deployment_error)) {
    std::cerr << "failed to apply phase-1 deployment: " << deployment_error << std::endl;
    return 1;
  }

  PrintSnapshot(supervisor_session.GetSnapshot());
  PrintDeploymentStatus(deployment_controller.GetStatus());
  supervisor_session.Stop();
  return 0;
}

}  // namespace evr::runtime::supervisor
