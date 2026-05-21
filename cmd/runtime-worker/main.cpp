#include "evr/runtime/worker/worker_app.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include "evr/runtime/config/runtime_config_loader.h"

namespace {

struct WorkerCliOptions {
  bool show_help{false};
  std::string config_path;
  bool has_source_session_id{false};
  std::string source_session_id;
  bool has_source_uri{false};
  std::string source_uri;
  bool has_decode_mode{false};
  std::string decode_mode;
  bool has_worker_session_id{false};
  std::string worker_session_id;
  bool has_worker_source_session_id{false};
  std::string worker_source_session_id;
  bool has_supervisor_endpoint{false};
  std::string supervisor_endpoint;
  bool has_proto_version{false};
  std::string proto_version;
  bool has_inference_backend{false};
  std::string inference_backend;
  bool has_engine_path{false};
  std::string engine_path;
  bool has_algorithm_package_uri{false};
  std::string algorithm_package_uri;
  bool has_algorithm_entry_point{false};
  std::string algorithm_entry_point;
  bool has_algorithm_runtime_config_uri{false};
  std::string algorithm_runtime_config_uri;
  bool decode_source_frames{false};
  bool has_frame_width{false};
  std::string frame_width;
  bool has_frame_height{false};
  std::string frame_height;
  bool has_frame_count{false};
  std::string frame_count;
  bool has_dump_dir{false};
  std::string dump_dir;
  bool has_event_store_dir{false};
  std::string event_store_dir;
  bool dump_negative_frames{false};
  bool has_observe_socket{false};
  std::string observe_socket;
  bool has_observe_preview_dir{false};
  std::string observe_preview_dir;
};

bool AssignOptionValue(const std::string& argument,
                       const std::string& name,
                       int argc,
                       char** argv,
                       int* index,
                       std::string* value,
                       std::string* error) {
  const std::string prefix = name + "=";
  if (argument == name) {
    if (*index + 1 >= argc) {
      *error = "missing value for " + name;
      return false;
    }

    *value = argv[++(*index)];
    return true;
  }

  if (argument.rfind(prefix, 0) == 0) {
    *value = argument.substr(prefix.size());
    return true;
  }

  return false;
}

bool ParseArgs(int argc, char** argv, WorkerCliOptions* options, std::string* error) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h" || argument == "help") {
      options->show_help = true;
      continue;
    }

    if (argument == "run") {
      continue;
    }

    if (AssignOptionValue(argument, "--config", argc, argv, &index, &options->config_path, error)) {
      continue;
    }

    if (AssignOptionValue(argument, "--source-session-id", argc, argv, &index,
                          &options->source_session_id, error)) {
      options->has_source_session_id = true;
      continue;
    }

    if (AssignOptionValue(argument, "--source-uri", argc, argv, &index, &options->source_uri, error)) {
      options->has_source_uri = true;
      continue;
    }

    if (AssignOptionValue(argument, "--decode-mode", argc, argv, &index, &options->decode_mode, error)) {
      options->has_decode_mode = true;
      continue;
    }

    if (AssignOptionValue(argument, "--worker-session-id", argc, argv, &index,
                          &options->worker_session_id, error)) {
      options->has_worker_session_id = true;
      continue;
    }

    if (AssignOptionValue(argument, "--worker-source-session-id", argc, argv, &index,
                          &options->worker_source_session_id, error)) {
      options->has_worker_source_session_id = true;
      continue;
    }

    if (AssignOptionValue(argument, "--supervisor-endpoint", argc, argv, &index,
                          &options->supervisor_endpoint, error)) {
      options->has_supervisor_endpoint = true;
      continue;
    }

    if (AssignOptionValue(argument, "--proto-version", argc, argv, &index, &options->proto_version,
                          error)) {
      options->has_proto_version = true;
      continue;
    }

    if (AssignOptionValue(argument, "--inference-backend", argc, argv, &index,
                          &options->inference_backend, error)) {
      options->has_inference_backend = true;
      continue;
    }

    if (AssignOptionValue(argument, "--engine-path", argc, argv, &index, &options->engine_path,
                          error)) {
      options->has_engine_path = true;
      continue;
    }

    if (AssignOptionValue(argument, "--algorithm-package-uri", argc, argv, &index,
                          &options->algorithm_package_uri, error)) {
      options->has_algorithm_package_uri = true;
      continue;
    }

    if (AssignOptionValue(argument, "--algorithm-entry-point", argc, argv, &index,
                          &options->algorithm_entry_point, error)) {
      options->has_algorithm_entry_point = true;
      continue;
    }

    if (AssignOptionValue(argument, "--algorithm-runtime-config-uri", argc, argv, &index,
                          &options->algorithm_runtime_config_uri, error)) {
      options->has_algorithm_runtime_config_uri = true;
      continue;
    }

    if (argument == "--decode-source-frames") {
      options->decode_source_frames = true;
      continue;
    }

    if (AssignOptionValue(argument, "--frame-width", argc, argv, &index, &options->frame_width,
                          error)) {
      options->has_frame_width = true;
      continue;
    }

    if (AssignOptionValue(argument, "--frame-height", argc, argv, &index, &options->frame_height,
                          error)) {
      options->has_frame_height = true;
      continue;
    }

    if (AssignOptionValue(argument, "--frame-count", argc, argv, &index, &options->frame_count,
                          error)) {
      options->has_frame_count = true;
      continue;
    }

    if (AssignOptionValue(argument, "--dump-dir", argc, argv, &index, &options->dump_dir, error)) {
      options->has_dump_dir = true;
      continue;
    }

    if (AssignOptionValue(argument, "--event-store-dir", argc, argv, &index,
                          &options->event_store_dir, error)) {
      options->has_event_store_dir = true;
      continue;
    }

    if (argument == "--dump-negative-frames") {
      options->dump_negative_frames = true;
      continue;
    }

    if (AssignOptionValue(argument, "--observe-socket", argc, argv, &index,
                          &options->observe_socket, error)) {
      options->has_observe_socket = true;
      continue;
    }

    if (AssignOptionValue(argument, "--observe-preview-dir", argc, argv, &index,
                          &options->observe_preview_dir, error)) {
      options->has_observe_preview_dir = true;
      continue;
    }

    *error = "unknown argument: " + argument;
    return false;
  }

  return true;
}

void PrintUsage() {
  std::cout << "Usage: runtime-worker [run] [--config FILE] [--source-session-id ID] "
               "[--source-uri URI] [--decode-mode MODE] [--worker-session-id ID] "
               "[--worker-source-session-id ID] [--supervisor-endpoint URI] "
               "[--proto-version VERSION] [--inference-backend NAME] [--engine-path PATH] "
               "[--algorithm-package-uri URI] [--algorithm-entry-point SYMBOL] "
               "[--algorithm-runtime-config-uri URI] [--decode-source-frames] "
               "[--frame-width WIDTH] [--frame-height HEIGHT] [--frame-count COUNT] "
               "[--dump-dir DIR] [--event-store-dir DIR] [--dump-negative-frames] "
               "[--observe-socket PATH] [--observe-preview-dir DIR]\n";
}

}  // namespace

int main(int argc, char** argv) {
  WorkerCliOptions options;
  std::string error;
  if (!ParseArgs(argc, argv, &options, &error)) {
    std::cerr << error << '\n';
    PrintUsage();
    return 1;
  }

  if (options.show_help) {
    PrintUsage();
    return 0;
  }

  evr::runtime::worker::WorkerAppConfig config;
  evr::runtime::config::RuntimeConfigLoader loader;
  if (!options.config_path.empty() && !loader.LoadWorkerAppConfig(options.config_path, &config, &error)) {
    std::cerr << error << '\n';
    return 1;
  }

  if (options.has_source_session_id) {
    config.source.session_id = options.source_session_id;
  }

  if (options.has_source_uri) {
    config.source.source_uri = options.source_uri;
  }

  if (options.has_decode_mode) {
    config.source.decode_mode = options.decode_mode;
  }

  if (options.has_worker_session_id) {
    config.worker.session_id = options.worker_session_id;
  }

  if (options.has_worker_source_session_id) {
    config.worker.source_session_id = options.worker_source_session_id;
  }

  if (options.has_supervisor_endpoint) {
    config.worker.supervisor_endpoint = options.supervisor_endpoint;
  }

  if (options.has_proto_version) {
    config.source.proto_version = options.proto_version;
    config.worker.proto_version = options.proto_version;
  }

  if (options.has_inference_backend) {
    config.worker.inference_backend = options.inference_backend;
  }

  if (options.has_engine_path) {
    config.worker.engine_path = options.engine_path;
  }

  if (options.has_algorithm_package_uri) {
    config.algorithm.package_uri = options.algorithm_package_uri;
    config.worker.algorithm_package_uri = options.algorithm_package_uri;
  }

  if (options.has_algorithm_entry_point) {
    config.algorithm.entry_point = options.algorithm_entry_point;
    config.worker.algorithm_entry_point = options.algorithm_entry_point;
  }

  if (options.has_algorithm_runtime_config_uri) {
    config.algorithm.runtime_config_uri = options.algorithm_runtime_config_uri;
    config.worker.algorithm_runtime_config_uri = options.algorithm_runtime_config_uri;
  }

  config.decode_source_frames = options.decode_source_frames;
  if (options.has_dump_dir) {
    config.dump_dir = options.dump_dir;
  }
  if (options.has_event_store_dir) {
    config.event_store_dir = options.event_store_dir;
  }
  config.dump_negative_frames = options.dump_negative_frames;
  if (options.has_observe_socket) {
    config.observability.enabled = true;
    config.observability.socket_path = options.observe_socket;
  }
  if (options.has_observe_preview_dir) {
    config.observability.preview_dir = options.observe_preview_dir;
  }
  try {
    if (options.has_frame_width) {
      config.source_frame_width = std::stoi(options.frame_width);
    }
    if (options.has_frame_height) {
      config.source_frame_height = std::stoi(options.frame_height);
    }
    if (options.has_frame_count) {
      config.source_frame_count = std::stoi(options.frame_count);
    }
  } catch (const std::exception& ex) {
    std::cerr << "invalid frame option: " << ex.what() << '\n';
    return 1;
  }
  if (config.source_frame_width <= 0 || config.source_frame_height <= 0 ||
      config.source_frame_count <= 0) {
    std::cerr << "frame width, height, and count must be positive\n";
    return 1;
  }

  if (options.has_source_session_id && !options.has_worker_source_session_id) {
    config.worker.source_session_id = config.source.session_id;
  }

  if (config.worker.source_session_id.empty()) {
    config.worker.source_session_id = config.source.session_id;
  }

  if (config.worker.proto_version.empty()) {
    config.worker.proto_version = config.source.proto_version;
  }

  evr::runtime::worker::WorkerApp app(config);
  return app.Run();
}
