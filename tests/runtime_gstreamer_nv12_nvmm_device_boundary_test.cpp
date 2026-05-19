#include <cassert>
#include <iostream>
#include <string>

#include "evr/runtime/source/source_session.h"

int main() {
  evr::runtime::source::SourceSession source_session;
  evr::runtime::source::SourceSessionConfig source_config;
  source_config.session_id = "gstreamer-nv12-nvmm-boundary";
  source_config.source_uri = "gst-testsrc://smpte";
  source_config.upstream_kind = "gstreamer-testsrc";
  source_config.transport_protocol = "in-process";
  source_config.buffer_transport = "nvmm";
  source_config.decode_mode = "gstreamer-nv12-nvmm-device";
  source_config.pixel_format = "nv12";
  source_config.decode_timeout_seconds = 10;
  assert(source_session.Configure(source_config));
  assert(source_session.Start());

  std::string error;
  const auto frames = source_session.CaptureFramesFromSource(64, 64, 1, &error);
  assert(frames.empty());
  if (error.find("device-resident path") == std::string::npos ||
      error.find("device-side preprocess") == std::string::npos) {
    std::cerr << "unexpected error: " << error << '\n';
    return 1;
  }

  source_session.Stop();
  return 0;
}
