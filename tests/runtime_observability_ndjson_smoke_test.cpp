#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "evr/runtime/observability/observe_publisher.h"

namespace {

std::string MakeSocketPath() {
  return (std::filesystem::temp_directory_path() /
          ("evr_observe_" + std::to_string(getpid()) + ".sock"))
      .string();
}

std::vector<std::string> RunSocketServer(const std::string& socket_path) {
  std::vector<std::string> lines;
  const int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  assert(server_fd >= 0);

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path.c_str());
  unlink(socket_path.c_str());
  assert(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  assert(listen(server_fd, 1) == 0);

  const int client_fd = accept(server_fd, nullptr, nullptr);
  assert(client_fd >= 0);

  std::string buffer;
  char chunk[256];
  while (lines.size() < 3U) {
    const ssize_t count = read(client_fd, chunk, sizeof(chunk));
    assert(count > 0);
    buffer.append(chunk, static_cast<std::size_t>(count));
    std::size_t newline = buffer.find('\n');
    while (newline != std::string::npos) {
      lines.push_back(buffer.substr(0, newline));
      buffer.erase(0, newline + 1U);
      newline = buffer.find('\n');
    }
  }

  close(client_fd);
  close(server_fd);
  unlink(socket_path.c_str());
  return lines;
}

}  // namespace

int main() {
  const std::string socket_path = MakeSocketPath();
  std::vector<std::string> lines;
  std::thread server([&]() { lines = RunSocketServer(socket_path); });

  for (int attempt = 0; attempt < 50; ++attempt) {
    if (std::filesystem::exists(socket_path)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(std::filesystem::exists(socket_path));

  evr::runtime::observability::ObservePublisher publisher;
  evr::runtime::observability::ObservePublisherConfig config;
  config.enabled = true;
  config.socket_path = socket_path;
  std::string error;
  assert(publisher.Configure(config, &error));

  evr::runtime::observability::StreamSnapshotEvent snapshot;
  snapshot.stream_id = "camera-0";
  snapshot.source_session_id = "camera-0";
  snapshot.decode_mode = "gstreamer-nv12-host";
  snapshot.pixel_format = "nv12";
  snapshot.buffer_transport = "host-memory";
  snapshot.preview_path = "/run/edge-vision/previews/camera-0/latest.jpg";
  snapshot.metadata_path = "/run/edge-vision/previews/camera-0/latest.json";
  snapshot.input_fps = 25.0;
  snapshot.decoded_fps = 24.8;
  snapshot.infer_fps = 12.5;
  snapshot.queue_depth = 1;
  snapshot.drop_total = 2;
  snapshot.frame_age_ms = 43;
  snapshot.last_detection_count = 1;
  snapshot.healthy = true;
  snapshot.latency.e2e_ms = 41;
  assert(publisher.PublishStreamSnapshot(snapshot, &error));

  evr::runtime::observability::FrontendOsdFrameEvent osd_frame;
  osd_frame.source_session_id = "camera-0";
  osd_frame.frame_id = 123;
  osd_frame.ts_ms = 1710000000000;
  osd_frame.decode_mode = "gstreamer-nv12-host";
  osd_frame.pixel_format = "nv12";
  osd_frame.buffer_transport = "host-memory";
  osd_frame.preview_path = "/run/edge-vision/previews/camera-0/frame_123.jpg";
  osd_frame.preview_width = 640;
  osd_frame.preview_height = 360;
  osd_frame.timing.infer_ms = 21;
  osd_frame.detections.push_back({0, "person", 0.88F, 208, 54, 224, 252});
  assert(publisher.PublishFrontendOsdFrame(osd_frame, &error));

  evr::runtime::observability::RuntimeEventRecord runtime_event;
  runtime_event.level = "warn";
  runtime_event.stream_id = "camera-0";
  runtime_event.event = "decode_timeout";
  runtime_event.message = "timed out pulling frame";
  runtime_event.ts_ms = 1710000000042;
  assert(publisher.PublishRuntimeEvent(runtime_event, &error));

  publisher.Close();
  server.join();

  assert(lines.size() == 3U);
  assert(lines[0].find("\"schema_version\":\"v1\"") != std::string::npos);
  assert(lines[0].find("\"producer\":{\"name\":\"edge-vision-runtime\",\"version\":\"0.1.0\"}") !=
         std::string::npos);
  assert(lines[0].find("\"type\":\"stream_snapshot\"") != std::string::npos);
  assert(lines[0].find("\"stream_id\":\"camera-0\"") != std::string::npos);
  assert(lines[1].find("\"schema_version\":\"v1\"") != std::string::npos);
  assert(lines[1].find("\"type\":\"frontend_osd_frame\"") != std::string::npos);
  assert(lines[1].find("\"preview\":{\"path\":\"/run/edge-vision/previews/camera-0/frame_123.jpg\"") !=
         std::string::npos);
  assert(lines[2].find("\"schema_version\":\"v1\"") != std::string::npos);
  assert(lines[2].find("\"type\":\"runtime_event\"") != std::string::npos);
  assert(lines[2].find("\"event\":\"decode_timeout\"") != std::string::npos);
  return 0;
}
