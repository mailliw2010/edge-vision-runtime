#include "evr/runtime/session/session_state.h"

namespace evr::runtime::session {

const char* ToString(State state) {
  switch (state) {
    case State::kCreated:
      return "created";
    case State::kConfigured:
      return "configured";
    case State::kRunning:
      return "running";
    case State::kStopped:
      return "stopped";
  }

  return "unknown";
}

}  // namespace evr::runtime::session
