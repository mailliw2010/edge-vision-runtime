#pragma once

#include "evr/runtime/supervisor/supervisor_session.h"

namespace evr::runtime::supervisor {

struct SupervisorAppConfig {
  SupervisorSessionConfig session{};
};

class SupervisorApp {
 public:
  explicit SupervisorApp(SupervisorAppConfig config = {});

  int Run();

 private:
  SupervisorAppConfig config_;
};

}  // namespace evr::runtime::supervisor
