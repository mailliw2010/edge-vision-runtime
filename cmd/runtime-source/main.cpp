#include "evr/runtime/source/source_app.h"

#include <iostream>
#include <string>

#include "evr/runtime/config/runtime_config_loader.h"

namespace {

struct SourceCliOptions {
  bool show_help{false};
  std::string config_path;
  bool has_session_id{false};
  std::string session_id;
  bool has_source_uri{false};
  std::string source_uri;
  bool has_decode_mode{false};
  std::string decode_mode;
  bool has_proto_version{false};
  std::string proto_version;
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

bool ParseArgs(int argc, char** argv, SourceCliOptions* options, std::string* error) {
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

    if (AssignOptionValue(argument, "--session-id", argc, argv, &index, &options->session_id, error)) {
      options->has_session_id = true;
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

    if (AssignOptionValue(argument, "--proto-version", argc, argv, &index, &options->proto_version,
                          error)) {
      options->has_proto_version = true;
      continue;
    }

    *error = "unknown argument: " + argument;
    return false;
  }

  return true;
}

void PrintUsage() {
  std::cout << "Usage: runtime-source [run] [--config FILE] [--session-id ID] [--source-uri URI] "
               "[--decode-mode MODE] [--proto-version VERSION]\n";
}

}  // namespace

int main(int argc, char** argv) {
  SourceCliOptions options;
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

  evr::runtime::source::SourceAppConfig config;
  evr::runtime::config::RuntimeConfigLoader loader;
  if (!options.config_path.empty() && !loader.LoadSourceAppConfig(options.config_path, &config, &error)) {
    std::cerr << error << '\n';
    return 1;
  }

  if (options.has_session_id) {
    config.session.session_id = options.session_id;
  }

  if (options.has_source_uri) {
    config.session.source_uri = options.source_uri;
  }

  if (options.has_decode_mode) {
    config.session.decode_mode = options.decode_mode;
  }

  if (options.has_proto_version) {
    config.session.proto_version = options.proto_version;
  }

  evr::runtime::source::SourceApp app(config);
  return app.Run();
}
