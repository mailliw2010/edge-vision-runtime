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

bool SupervisorApp::BuildDeploymentSpec(deployment::Phase1DeploymentSpec* spec,
                                        std::string* error) const {
  if (spec == nullptr) {
    if (error != nullptr) {
      *error = "deployment spec output is null";
    }
    return false;
  }

  *spec = config_.deployment;
  spec->supervisor = config_.session;
  return true;
}

int SupervisorApp::ApplyDeployment() {
  deployment::Phase1DeploymentSpec deployment_spec;
  std::string deployment_error;
  if (!BuildDeploymentSpec(&deployment_spec, &deployment_error)) {
    std::cerr << "failed to build phase-1 deployment: " << deployment_error << std::endl;
    return 1;
  }

  deployment::DeploymentController deployment_controller;
  if (!deployment_controller.Apply(std::move(deployment_spec), &deployment_error)) {
    std::cerr << "failed to apply phase-1 deployment: " << deployment_error << std::endl;
    return 1;
  }

  PrintDeploymentStatus(deployment_controller.GetStatus());
  return 0;
}

int SupervisorApp::ShowDeploymentStatus() const {
  deployment::Phase1DeploymentSpec deployment_spec;
  std::string deployment_error;
  if (!BuildDeploymentSpec(&deployment_spec, &deployment_error)) {
    std::cerr << "failed to build phase-1 deployment: " << deployment_error << std::endl;
    return 1;
  }

  if (!deployment_spec.Normalize(&deployment_error)) {
    std::cerr << "failed to inspect phase-1 deployment: " << deployment_error << std::endl;
    return 1;
  }

  deployment::DeploymentStatus status;
  status.deployment_id = deployment_spec.deployment_id;
  status.wiring = deployment_spec.DescribeWiring();
  status.state = session::State::kConfigured;
  status.detail = "desired topology from config only; persisted deployment status is not wired yet";
  PrintDeploymentStatus(status);
  return 0;
}

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
  const int apply_result = ApplyDeployment();
  supervisor_session.Stop();
  return apply_result;
}

}  // namespace evr::runtime::supervisor
