#pragma once

#include <string>

namespace evr::runtime::session {

enum class State {
  kCreated,
  kConfigured,
  kRunning,
  kStopped,
};

struct Snapshot {
  std::string kind;
  std::string session_id;
  State state{State::kCreated};
  std::string detail;
};

const char* ToString(State state);

}  // namespace evr::runtime::session
