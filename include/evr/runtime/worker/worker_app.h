#pragma once

#include <string>

#include "evr/runtime/source/source_session.h"
#include "evr/runtime/worker/worker_session.h"

namespace evr::runtime::worker {

struct AlgorithmPackageConfig {
  std::string package_uri{};
  std::string entry_point{};
  std::string runtime_config_uri{};
  std::string model_path{};
};

struct WorkerAppConfig {
  source::SourceSessionConfig source{};
  WorkerSessionConfig worker{};
  AlgorithmPackageConfig algorithm{};
  bool decode_source_frames{false};
  int source_frame_width{640};
  int source_frame_height{640};
  int source_frame_count{1};
  std::string dump_dir{};
  std::string event_store_dir{};
  bool dump_negative_frames{false};
};

class WorkerApp {
 public:
  explicit WorkerApp(WorkerAppConfig config = {});

  int Run();

 private:
  WorkerAppConfig config_;
};

}  // namespace evr::runtime::worker
