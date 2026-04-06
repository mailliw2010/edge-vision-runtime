#pragma once

#include <string>

#include "evr/runtime/source/source_app.h"
#include "evr/runtime/supervisor/supervisor_app.h"
#include "evr/runtime/worker/worker_app.h"

namespace evr::runtime::config {

class RuntimeConfigLoader {
 public:
  bool LoadSourceAppConfig(const std::string& file_path,
                           source::SourceAppConfig* config,
                           std::string* error = nullptr) const;

  bool LoadSupervisorAppConfig(const std::string& file_path,
                               supervisor::SupervisorAppConfig* config,
                               std::string* error = nullptr) const;

  bool LoadWorkerAppConfig(const std::string& file_path,
                           worker::WorkerAppConfig* config,
                           std::string* error = nullptr) const;
};

}  // namespace evr::runtime::config
