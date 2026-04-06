#pragma once

#include "evr/runtime/source/source_session.h"
#include "evr/runtime/worker/worker_session.h"

namespace evr::runtime::worker {

struct WorkerAppConfig {
  source::SourceSessionConfig source{};
  WorkerSessionConfig worker{};
};

class WorkerApp {
 public:
  explicit WorkerApp(WorkerAppConfig config = {});

  int Run();

 private:
  WorkerAppConfig config_;
};

}  // namespace evr::runtime::worker
