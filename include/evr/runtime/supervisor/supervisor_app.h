#pragma once

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

 private:
  SupervisorAppConfig config_;
};

}  // namespace evr::runtime::supervisor
