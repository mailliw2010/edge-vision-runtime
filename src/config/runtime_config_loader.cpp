#include "evr/runtime/config/runtime_config_loader.h"

#include <cstddef>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace evr::runtime::config {
namespace {

using SectionMap = std::map<std::string, std::map<std::string, std::string>>;

std::string Trim(std::string value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }

  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string StripComment(std::string value) {
  const std::size_t comment = value.find('#');
  if (comment == std::string::npos) {
    return value;
  }

  return value.substr(0, comment);
}

std::string Unquote(std::string value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }

  return value;
}

bool LoadDocument(const std::string& file_path, SectionMap* sections, std::string* error) {
  if (sections == nullptr) {
    if (error != nullptr) {
      *error = "sections output is null";
    }
    return false;
  }

  std::ifstream input(file_path);
  if (!input) {
    if (error != nullptr) {
      *error = "failed to open config file: " + file_path;
    }
    return false;
  }

  std::string current_section;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;

    const std::size_t indent = line.find_first_not_of(" ");
    const bool is_top_level = indent == 0 || indent == std::string::npos;
    std::string normalized = Trim(StripComment(line));
    if (normalized.empty()) {
      continue;
    }

    if (is_top_level && normalized.back() == ':' && normalized.find(':') == normalized.size() - 1) {
      current_section = Trim(normalized.substr(0, normalized.size() - 1));
      continue;
    }

    const std::size_t separator = normalized.find(':');
    if (separator == std::string::npos || current_section.empty()) {
      if (error != nullptr) {
        std::ostringstream oss;
        oss << "unsupported config syntax at line " << line_number;
        *error = oss.str();
      }
      return false;
    }

    const std::string key = Trim(normalized.substr(0, separator));
    const std::string value = Unquote(Trim(normalized.substr(separator + 1)));
    (*sections)[current_section][key] = value;
  }

  return true;
}

std::string GetValue(const SectionMap& sections,
                     const std::string& section,
                     const std::string& key,
                     const std::string& fallback) {
  const auto section_it = sections.find(section);
  if (section_it == sections.end()) {
    return fallback;
  }

  const auto value_it = section_it->second.find(key);
  if (value_it == section_it->second.end() || value_it->second.empty()) {
    return fallback;
  }

  return value_it->second;
}

}  // namespace

bool RuntimeConfigLoader::LoadSourceAppConfig(const std::string& file_path,
                                              source::SourceAppConfig* config,
                                              std::string* error) const {
  if (config == nullptr) {
    if (error != nullptr) {
      *error = "source config output is null";
    }
    return false;
  }

  SectionMap sections;
  if (!LoadDocument(file_path, &sections, error)) {
    return false;
  }

  config->session.session_id = GetValue(sections, "source", "session_id", config->session.session_id);
  config->session.source_uri = GetValue(sections, "source", "source_uri", config->session.source_uri);
  config->session.proto_version =
      GetValue(sections, "source", "proto_version", config->session.proto_version);
  config->session.decode_mode = GetValue(sections, "source", "decode_mode", config->session.decode_mode);
  return true;
}

bool RuntimeConfigLoader::LoadSupervisorAppConfig(const std::string& file_path,
                                                  supervisor::SupervisorAppConfig* config,
                                                  std::string* error) const {
  if (config == nullptr) {
    if (error != nullptr) {
      *error = "supervisor config output is null";
    }
    return false;
  }

  SectionMap sections;
  if (!LoadDocument(file_path, &sections, error)) {
    return false;
  }

  config->session.session_id =
      GetValue(sections, "supervisor", "session_id", config->session.session_id);
  config->session.proto_version =
      GetValue(sections, "supervisor", "proto_version", config->session.proto_version);
  config->session.control_endpoint =
      GetValue(sections, "supervisor", "control_endpoint", config->session.control_endpoint);

  config->deployment.supervisor = config->session;
  return true;
}

bool RuntimeConfigLoader::LoadWorkerAppConfig(const std::string& file_path,
                                              worker::WorkerAppConfig* config,
                                              std::string* error) const {
  if (config == nullptr) {
    if (error != nullptr) {
      *error = "worker config output is null";
    }
    return false;
  }

  SectionMap sections;
  if (!LoadDocument(file_path, &sections, error)) {
    return false;
  }

  config->source.session_id = GetValue(sections, "source", "session_id", config->source.session_id);
  config->source.source_uri = GetValue(sections, "source", "source_uri", config->source.source_uri);
  config->source.proto_version = GetValue(sections, "source", "proto_version", config->source.proto_version);
  config->source.decode_mode = GetValue(sections, "source", "decode_mode", config->source.decode_mode);

  config->worker.session_id = GetValue(sections, "worker", "session_id", config->worker.session_id);
  config->worker.supervisor_endpoint =
      GetValue(sections, "worker", "supervisor_endpoint", config->worker.supervisor_endpoint);
  config->worker.source_session_id =
      GetValue(sections, "worker", "source_session_id", config->worker.source_session_id);
  config->worker.proto_version = GetValue(sections, "worker", "proto_version", config->worker.proto_version);
  config->worker.inference_backend =
      GetValue(sections, "worker", "inference_backend", config->worker.inference_backend);
  config->worker.engine_path = GetValue(sections, "worker", "engine_path", config->worker.engine_path);
  return true;
}

}  // namespace evr::runtime::config
