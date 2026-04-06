#pragma once

#include <string_view>

#include "evr/runtime/session/session_state.h"

namespace evr::runtime::session {

class LifecycleSession {
 public:
  virtual ~LifecycleSession() = default;

  virtual std::string_view Kind() const = 0;
  virtual State state() const = 0;
  virtual bool Start() = 0;
  virtual void Stop() = 0;
  virtual Snapshot GetSnapshot() const = 0;
};

}  // namespace evr::runtime::session
