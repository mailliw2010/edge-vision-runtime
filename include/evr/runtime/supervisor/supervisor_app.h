#pragma once

#include <string>

#include "evr/runtime/deployment/phase1_deployment.h"
#include "evr/runtime/supervisor/supervisor_session.h"

namespace evr::runtime::supervisor {

struct SupervisorAppConfig {
  SupervisorSessionConfig session{};
  deployment::Phase1DeploymentSpec deployment{};
};

class SupervisorApp {
 public:
  explicit SupervisorApp(SupervisorAppConfig config = {});

  int Run();
  int ApplyDeployment();
  int ShowDeploymentStatus() const;

 private:
  bool BuildDeploymentSpec(deployment::Phase1DeploymentSpec* spec,
                           std::string* error = nullptr) const;

  SupervisorAppConfig config_;
};

}  // namespace evr::runtime::supervisor
