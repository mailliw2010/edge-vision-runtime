#include <cassert>
#include <chrono>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "evr/runtime/worker/worker_app.h"

namespace {

std::string MakeSocketPath() {
  return (std::filesystem::temp_directory_path() /
          ("evr_worker_observe_" + std::to_string(getpid()) + ".sock"))
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
  char chunk[512];
  while (lines.size() < 2U) {
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
  const auto preview_dir =
      std::filesystem::temp_directory_path() / ("evr_worker_observe_preview_" + std::to_string(getpid()));

  std::vector<std::string> lines;
  std::thread server([&]() { lines = RunSocketServer(socket_path); });
  for (int attempt = 0; attempt < 50; ++attempt) {
    if (std::filesystem::exists(socket_path)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(std::filesystem::exists(socket_path));

  const auto project_root = std::filesystem::path(__FILE__).parent_path().parent_path();

  evr::runtime::worker::WorkerAppConfig config;
  config.source.session_id = "worker-observe-source";
  config.source.source_uri = "synthetic://rgba-gray-frames";
  config.source.upstream_kind = "synthetic";
  config.source.transport_protocol = "in-process";
  config.source.buffer_transport = "host-memory";
  config.source.decode_mode = "synthetic-rgba";
  config.source.pixel_format = "rgba";

  config.worker.session_id = "worker-observe-0";
  config.worker.source_session_id = config.source.session_id;
  config.worker.inference_backend = "synthetic";
  config.worker.engine_path = EVR_TEST_YOLOV8S_MODEL_PATH;
  config.worker.algorithm_name = "yolov8-person-detection";
  config.worker.input_binding = "frames";
  config.worker.result_encoding = "json";
  config.worker.output_topic = "events.detection.observe";

  config.algorithm.package_uri =
      (project_root / "algorithm/yolov8_person_detection").string();
  config.algorithm.entry_point =
      "evr::algorithm::yolov8_person_detection::YoloV8PersonDetector";
  config.algorithm.runtime_config_uri =
      (project_root /
       "algorithm/yolov8_person_detection/configs/yolov8_person_detection.v1.example.yaml")
          .string();
  config.algorithm.model_path = EVR_TEST_YOLOV8S_MODEL_PATH;

  config.observability.enabled = true;
  config.observability.socket_path = socket_path;
  config.observability.preview_dir = preview_dir.string();

  evr::runtime::worker::WorkerApp app(config);
  assert(app.Run() == 0);

  server.join();

  assert(lines.size() >= 2U);
  assert(lines[0].find("\"schema_version\":\"v1\"") != std::string::npos);
  assert(lines[0].find("\"source_session_id\":\"worker-observe-source\"") != std::string::npos);
  assert(lines[1].find("\"type\":\"stream_snapshot\"") != std::string::npos ||
         lines[0].find("\"type\":\"stream_snapshot\"") != std::string::npos);

  const auto latest_preview = preview_dir / config.source.session_id / "latest.jpg";
  const auto latest_sidecar = preview_dir / config.source.session_id / "latest.osd.json";
  assert(std::filesystem::exists(latest_preview));
  assert(std::filesystem::exists(latest_sidecar));

  std::filesystem::remove_all(preview_dir);
  return 0;
}
