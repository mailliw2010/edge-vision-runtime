#include "evr/runtime/source/source_session.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <utility>

namespace evr::runtime::source {
namespace {

std::string ShellQuote(const std::string& value) {
  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

std::string RedactUri(const std::string& uri) {
  const auto scheme = uri.find("://");
  if (scheme == std::string::npos) {
    return uri;
  }
  const auto at = uri.find('@', scheme + 3);
  if (at == std::string::npos) {
    return uri;
  }
  return uri.substr(0, scheme + 3) + "***:***@" + uri.substr(at + 1);
}

bool IsRtspUri(const std::string& uri) {
  return uri.rfind("rtsp://", 0) == 0 || uri.rfind("rtsps://", 0) == 0;
}

std::vector<std::uint8_t> ReadCommandBytes(const std::string& command) {
  std::vector<std::uint8_t> bytes;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return bytes;
  }

  std::array<unsigned char, 8192> buffer{};
  while (true) {
    const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), pipe);
    if (read > 0) {
      bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
    }
    if (read < buffer.size()) {
      if (std::feof(pipe) != 0) {
        break;
      }
      if (std::ferror(pipe) != 0) {
        bytes.clear();
        break;
      }
    }
  }

  if (pclose(pipe) != 0) {
    bytes.clear();
  }
  return bytes;
}

}  // namespace

bool SourceSession::Configure(const SourceSessionConfig& config) {
  if (state_ == session::State::kRunning) {
    return false;
  }

  config_ = config;
  state_ = session::State::kConfigured;
  return true;
}

std::string_view SourceSession::Kind() const { return "source"; }

session::State SourceSession::state() const { return state_; }

bool SourceSession::Start() {
  if (state_ != session::State::kConfigured) {
    return false;
  }

  state_ = session::State::kRunning;
  return true;
}

void SourceSession::Stop() {
  if (state_ == session::State::kRunning) {
    state_ = session::State::kStopped;
  }
}

session::Snapshot SourceSession::GetSnapshot() const {
  session::Snapshot snapshot;
  snapshot.kind = std::string(Kind());
  snapshot.session_id = config_.session_id;
  snapshot.state = state_;
  snapshot.detail = "uri=" + config_.source_uri + ", upstream_kind=" + config_.upstream_kind +
                    ", upstream_endpoint=" + config_.upstream_endpoint + ", transport=" +
                    config_.transport_protocol + ", buffer=" + config_.buffer_transport +
                    ", proto=" + config_.proto_version + ", decode=" + config_.decode_mode +
                    ", pixel=" + config_.pixel_format;
  return snapshot;
}

FrameBuffer SourceSession::MakeSyntheticFrame(int width, int height) const {
  FrameBuffer frame;
  frame.width = width;
  frame.height = height;
  frame.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 127U);
  return frame;
}

std::vector<FrameBuffer> SourceSession::CaptureFramesFromSource(int width,
                                                                int height,
                                                                int frame_count,
                                                                std::string* error) const {
  if (width <= 0 || height <= 0 || frame_count <= 0) {
    if (error != nullptr) {
      *error = "invalid source capture shape";
    }
    return {};
  }
  if (config_.source_uri.empty()) {
    if (error != nullptr) {
      *error = "source uri is empty";
    }
    return {};
  }

  std::string command = "ffmpeg -hide_banner -loglevel error ";
  if (IsRtspUri(config_.source_uri)) {
    command += "-rtsp_transport tcp -stimeout 10000000 ";
  }
  command += "-i " + ShellQuote(config_.source_uri) + " -an -frames:v " +
             std::to_string(frame_count) + " -vf scale=" + std::to_string(width) + ":" +
             std::to_string(height) + " -f rawvideo -pix_fmt rgba - "
             "2>/tmp/evr_runtime_source_ffmpeg.log";

  const auto raw_video = ReadCommandBytes(command);
  const std::size_t frame_bytes =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
  const std::size_t expected_bytes = frame_bytes * static_cast<std::size_t>(frame_count);
  if (raw_video.size() != expected_bytes) {
    if (error != nullptr) {
      *error = "failed to decode " + std::to_string(frame_count) + " frame(s) from " +
               RedactUri(config_.source_uri) + ": expected " + std::to_string(expected_bytes) +
               " bytes, got " + std::to_string(raw_video.size());
    }
    return {};
  }

  std::vector<FrameBuffer> frames;
  frames.reserve(static_cast<std::size_t>(frame_count));
  for (int frame_id = 0; frame_id < frame_count; ++frame_id) {
    const auto first = raw_video.begin() + static_cast<std::ptrdiff_t>(frame_bytes * frame_id);
    const auto last = first + static_cast<std::ptrdiff_t>(frame_bytes);
    FrameBuffer frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(first, last);
    frames.push_back(std::move(frame));
  }
  return frames;
}

}  // namespace evr::runtime::source
