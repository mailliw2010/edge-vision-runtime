#pragma once

#include "evr/runtime/source/source_session.h"

namespace evr::runtime::source {

struct SourceAppConfig {
  SourceSessionConfig session{};
};

class SourceApp {
 public:
  explicit SourceApp(SourceAppConfig config = {});

  int Run();

 private:
  SourceAppConfig config_;
};

}  // namespace evr::runtime::source
