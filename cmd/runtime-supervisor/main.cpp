#include "evr/runtime/supervisor/supervisor_app.h"

#include <iostream>
#include <string>

#include "evr/runtime/config/runtime_config_loader.h"

namespace {

enum class SupervisorCommand { kRun, kApply, kStatus };

struct SupervisorCliOptions {
  SupervisorCommand command{SupervisorCommand::kRun};
  bool show_help{false};
  std::string config_path;
  bool has_deployment_id{false};
  std::string deployment_id;
  bool has_session_id{false};
  std::string session_id;
  bool has_control_endpoint{false};
  std::string control_endpoint;
  bool has_proto_version{false};
  std::string proto_version;
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
  bool has_worker_supervisor_endpoint{false};
  std::string worker_supervisor_endpoint;
  bool has_inference_backend{false};
  std::string inference_backend;
  bool has_engine_path{false};
  std::string engine_path;
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

bool ParseArgs(int argc, char** argv, SupervisorCliOptions* options, std::string* error) {
  bool command_set = false;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h" || argument == "help") {
      options->show_help = true;
      continue;
    }

    if (!command_set && (argument == "run" || argument == "apply" || argument == "status")) {
      command_set = true;
      if (argument == "apply") {
        options->command = SupervisorCommand::kApply;
      } else if (argument == "status") {
        options->command = SupervisorCommand::kStatus;
      } else {
        options->command = SupervisorCommand::kRun;
      }
      continue;
    }

    if (AssignOptionValue(argument, "--config", argc, argv, &index, &options->config_path, error)) {
      continue;
    }

    if (AssignOptionValue(argument, "--deployment-id", argc, argv, &index, &options->deployment_id,
                          error)) {
      options->has_deployment_id = true;
      continue;
    }

    if (AssignOptionValue(argument, "--session-id", argc, argv, &index, &options->session_id,
                          error)) {
      options->has_session_id = true;
      continue;
    }

    if (AssignOptionValue(argument, "--control-endpoint", argc, argv, &index,
                          &options->control_endpoint, error)) {
      options->has_control_endpoint = true;
      continue;
    }

    if (AssignOptionValue(argument, "--proto-version", argc, argv, &index, &options->proto_version,
                          error)) {
      options->has_proto_version = true;
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

    if (AssignOptionValue(argument, "--decode-mode", argc, argv, &index, &options->decode_mode,
                          error)) {
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

    if (AssignOptionValue(argument, "--worker-supervisor-endpoint", argc, argv, &index,
                          &options->worker_supervisor_endpoint, error)) {
      options->has_worker_supervisor_endpoint = true;
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

    *error = "unknown argument: " + argument;
    return false;
  }

  return true;
}

void PrintUsage() {
  std::cout << "Usage: runtime-supervisor [run|apply|status] [--config FILE] [--deployment-id ID] "
               "[--session-id ID] [--control-endpoint URI] [--proto-version VERSION] "
               "[--source-session-id ID] [--source-uri URI] [--decode-mode MODE] "
               "[--worker-session-id ID] [--worker-source-session-id ID] "
               "[--worker-supervisor-endpoint URI] [--inference-backend NAME] [--engine-path PATH]\n";
}

}  // namespace

int main(int argc, char** argv) {
  SupervisorCliOptions options;
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

  evr::runtime::supervisor::SupervisorAppConfig config;
  evr::runtime::config::RuntimeConfigLoader loader;
  if (!options.config_path.empty() && !loader.LoadSupervisorAppConfig(options.config_path, &config, &error)) {
    std::cerr << error << '\n';
    return 1;
  }

  if (options.has_deployment_id) {
    config.deployment.deployment_id = options.deployment_id;
  }

  if (options.has_session_id) {
    config.deployment.supervisor.session_id = options.session_id;
  }

  if (options.has_control_endpoint) {
    config.deployment.supervisor.control_endpoint = options.control_endpoint;
  }

  if (options.has_proto_version) {
    config.deployment.supervisor.proto_version = options.proto_version;
    config.deployment.source.proto_version = options.proto_version;
    config.deployment.worker.proto_version = options.proto_version;
  }

  if (options.has_source_session_id) {
    config.deployment.source.session_id = options.source_session_id;
  }

  if (options.has_source_uri) {
    config.deployment.source.source_uri = options.source_uri;
  }

  if (options.has_decode_mode) {
    config.deployment.source.decode_mode = options.decode_mode;
  }

  if (options.has_worker_session_id) {
    config.deployment.worker.session_id = options.worker_session_id;
  }

  if (options.has_worker_source_session_id) {
    config.deployment.worker.source_session_id = options.worker_source_session_id;
  }

  if (options.has_worker_supervisor_endpoint) {
    config.deployment.worker.supervisor_endpoint = options.worker_supervisor_endpoint;
  }

  if (options.has_inference_backend) {
    config.deployment.worker.inference_backend = options.inference_backend;
  }

  if (options.has_engine_path) {
    config.deployment.worker.engine_path = options.engine_path;
  }

  if (options.has_source_session_id && !options.has_worker_source_session_id) {
    config.deployment.worker.source_session_id = config.deployment.source.session_id;
  }

  if (options.has_control_endpoint && !options.has_worker_supervisor_endpoint) {
    config.deployment.worker.supervisor_endpoint = config.deployment.supervisor.control_endpoint;
  }

  config.session = config.deployment.supervisor;

  evr::runtime::supervisor::SupervisorApp app(config);
  switch (options.command) {
    case SupervisorCommand::kApply:
      return app.ApplyDeployment();
    case SupervisorCommand::kStatus:
      return app.ShowDeploymentStatus();
    case SupervisorCommand::kRun:
      return app.Run();
  }

  return 1;
}
