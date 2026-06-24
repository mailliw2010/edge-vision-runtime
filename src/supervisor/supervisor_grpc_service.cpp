#include "evr/runtime/supervisor/supervisor_grpc_service.h"

#include <set>
#include <sstream>
#include <utility>

#include "evr/runtime/graph/graph.h"

namespace evr::runtime::supervisor {
namespace {

constexpr const char* kProtocolVersion = "runtime.v1";

void FillRejectedApply(const std::string& deployment_id,
                       const std::string& reason,
                       edgevision::runtime::v1::ApplyDeploymentResponse* response) {
  response->set_accepted(false);
  response->set_operation_id("rejected:" + deployment_id + ":" + reason);
}

std::string ExecutionModeToString(edgevision::common::v1::ExecutionMode mode) {
  switch (mode) {
    case edgevision::common::v1::EXECUTION_MODE_LOCAL_FRAME:
      return "local_frame";
    case edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC:
      return "remote_grpc";
    case edgevision::common::v1::EXECUTION_MODE_REMOTE_HTTP:
      return "remote_http";
    default:
      return "unspecified";
  }
}

std::string ExecutionBackendToString(edgevision::common::v1::ExecutionBackend backend) {
  switch (backend) {
    case edgevision::common::v1::EXECUTION_BACKEND_TENSORRT:
      return "tensorrt";
    case edgevision::common::v1::EXECUTION_BACKEND_CPU:
      return "cpu";
    case edgevision::common::v1::EXECUTION_BACKEND_FASTDEPLOY:
      return "fastdeploy";
    case edgevision::common::v1::EXECUTION_BACKEND_DEEPSTREAM:
      return "deepstream";
    case edgevision::common::v1::EXECUTION_BACKEND_TRITON:
      return "triton";
    case edgevision::common::v1::EXECUTION_BACKEND_ONNX_RUNTIME:
      return "onnxruntime";
    default:
      return "unspecified";
  }
}

std::string JsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string ResourceRefToJson(const edgevision::common::v1::ResourceRef& ref) {
  std::ostringstream out;
  out << "{\"kind\":\"" << JsonEscape(ref.kind()) << "\",";
  out << "\"id\":\"" << JsonEscape(ref.id()) << "\",";
  out << "\"revision\":\"" << JsonEscape(ref.revision()) << "\"}";
  return out.str();
}

std::string OutputBindingToJson(const edgevision::runtime::v1::RuntimeOutputBinding& binding) {
  std::ostringstream out;
  out << "{\"binding_id\":\"" << JsonEscape(binding.binding_id()) << "\",";
  out << "\"kind\":\"" << JsonEscape(binding.kind()) << "\",";
  out << "\"sink\":" << ResourceRefToJson(binding.sink()) << "}";
  return out.str();
}

std::string ParametersToJson(const google::protobuf::Map<std::string, std::string>& parameters) {
  std::ostringstream out;
  out << '{';
  bool first = true;
  for (const auto& [key, value] : parameters) {
    if (!first) {
      out << ',';
    }
    first = false;
    out << "\"" << JsonEscape(key) << "\":\"" << JsonEscape(value) << "\"";
  }
  out << '}';
  return out.str();
}

std::string LabelsToJson(const google::protobuf::Map<std::string, std::string>& labels) {
  return ParametersToJson(labels);
}

std::string SourceBindingToJson(const edgevision::runtime::v1::RuntimeSourceBinding& binding) {
  std::ostringstream out;
  out << "{\"binding_id\":\"" << JsonEscape(binding.binding_id()) << "\",";
  out << "\"input_binding\":" << ResourceRefToJson(binding.input_binding()) << ",";
  out << "\"labels\":" << LabelsToJson(binding.labels()) << "}";
  return out.str();
}

std::string AlgorithmBindingToJson(const edgevision::runtime::v1::RuntimeAlgorithmBinding& binding) {
  std::ostringstream out;
  out << "{\"binding_id\":\"" << JsonEscape(binding.binding_id()) << "\",";
  out << "\"algorithm\":" << ResourceRefToJson(binding.algorithm()) << ",";
  out << "\"artifact\":" << ResourceRefToJson(binding.artifact()) << ",";
  out << "\"config_revision\":" << ResourceRefToJson(binding.config_revision()) << ",";
  out << "\"execution_mode\":\"" << ExecutionModeToString(binding.execution_mode()) << "\",";
  out << "\"execution_backend\":\"" << ExecutionBackendToString(binding.execution_backend()) << "\",";
  out << "\"input_binding_ids\":[";
  for (int i = 0; i < binding.input_binding_ids_size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << "\"" << JsonEscape(binding.input_binding_ids(i)) << "\"";
  }
  out << "],";
  out << "\"parameters\":" << ParametersToJson(binding.parameters()) << "}";
  return out.str();
}

evr::runtime::graph::Node MakeSupervisorNode(const SupervisorGrpcServiceConfig& config) {
  evr::runtime::graph::Node node;
  node.id = config.node_id;
  node.type = "control";
  node.subtype = "supervisor";
  node.name = config.runtime_version;
  node.outputs = {"control", "status"};
  std::ostringstream out;
  out << "{\"node_id\":\"" << JsonEscape(config.node_id) << "\",";
  out << "\"runtime_version\":\"" << JsonEscape(config.runtime_version) << "\"}";
  node.config_ref = out.str();
  return node;
}

evr::runtime::graph::Node MakeSourceNode(const edgevision::runtime::v1::RuntimeSourceBinding& binding) {
  evr::runtime::graph::Node node;
  node.id = binding.binding_id();
  node.type = "media";
  node.subtype = binding.input_binding().kind().empty() ? "source" : binding.input_binding().kind();
  node.name = binding.input_binding().id();
  node.outputs = {"frames"};
  node.config_ref = SourceBindingToJson(binding);
  return node;
}

evr::runtime::graph::Node MakeAlgorithmNode(
    const edgevision::runtime::v1::RuntimeAlgorithmBinding& binding) {
  evr::runtime::graph::Node node;
  node.id = binding.binding_id();
  node.type = "inference";
  node.subtype = ExecutionBackendToString(binding.execution_backend());
  node.name = binding.algorithm().id().empty() ? binding.artifact().id() : binding.algorithm().id();
  node.inputs.reserve(static_cast<std::size_t>(binding.input_binding_ids_size()));
  for (const auto& input_id : binding.input_binding_ids()) {
    node.inputs.push_back(input_id);
  }
  node.outputs = {"results"};
  node.config_ref = AlgorithmBindingToJson(binding);
  return node;
}

evr::runtime::graph::Node MakeOutputNode(const edgevision::runtime::v1::RuntimeOutputBinding& binding,
                                        const std::string& fallback_id) {
  evr::runtime::graph::Node node;
  node.id = binding.binding_id().empty() ? fallback_id : binding.binding_id();
  node.type = "output";
  node.subtype = binding.kind().empty() ? "sink" : binding.kind();
  node.name = node.id;
  node.inputs = {"results"};
  node.config_ref = OutputBindingToJson(binding);
  return node;
}

evr::runtime::graph::Edge MakeEdge(const std::string& id,
                                   const std::string& from_node,
                                   const std::string& from_port,
                                   const std::string& to_node,
                                   const std::string& to_port,
                                   const std::string& type) {
  evr::runtime::graph::Edge edge;
  edge.id = id;
  edge.from_node = from_node;
  edge.from_port = from_port;
  edge.to_node = to_node;
  edge.to_port = to_port;
  edge.type = type;
  return edge;
}

evr::runtime::graph::Graph BuildExecutionGraph(const edgevision::runtime::v1::ApplyDeploymentRequest& request,
                                              const std::string& node_id,
                                              const std::string& runtime_version) {
  evr::runtime::graph::Graph graph;
  const auto& spec = request.spec();
  const auto& execution = spec.execution();
  graph.id = spec.deployment_id();

  graph.nodes.push_back(MakeSupervisorNode(SupervisorGrpcServiceConfig{
      node_id,
      runtime_version,
      8,
      1,
  }));

  std::set<std::string> source_ids;
  for (const auto& source_binding : execution.sources()) {
    source_ids.insert(source_binding.binding_id());
    graph.nodes.push_back(MakeSourceNode(source_binding));
  }

  for (const auto& algorithm_binding : execution.algorithms()) {
    graph.nodes.push_back(MakeAlgorithmNode(algorithm_binding));
  }

  if (execution.outputs_size() == 0) {
    edgevision::runtime::v1::RuntimeOutputBinding output_binding;
    output_binding.set_binding_id(spec.deployment_id() + ":sink");
    output_binding.set_kind("event-topic");
    output_binding.mutable_sink()->set_kind("deployment");
    output_binding.mutable_sink()->set_id(spec.deployment_id());
    graph.nodes.push_back(MakeOutputNode(output_binding, spec.deployment_id() + ":sink"));
  } else {
    for (const auto& output_binding : execution.outputs()) {
      graph.nodes.push_back(MakeOutputNode(output_binding, spec.deployment_id() + ":sink"));
    }
  }

  std::set<std::string> downstream_bindings;
  for (const auto& algorithm_binding : execution.algorithms()) {
    for (const auto& input_id : algorithm_binding.input_binding_ids()) {
      downstream_bindings.insert(input_id);
      const std::string edge_type = source_ids.find(input_id) != source_ids.end() ? "frame" : "event";
      const std::string from_port = source_ids.find(input_id) != source_ids.end() ? "frames" : "results";
      graph.edges.push_back(MakeEdge(
          spec.deployment_id() + ":" + input_id + "->" + algorithm_binding.binding_id(),
          input_id,
          from_port,
          algorithm_binding.binding_id(),
          input_id,
          edge_type));
    }
    graph.edges.push_back(MakeEdge(
        spec.deployment_id() + ":supervisor->" + algorithm_binding.binding_id(),
        graph.nodes.front().id,
        "control",
        algorithm_binding.binding_id(),
        "control",
        "control"));
  }

  std::set<std::string> leaf_algorithms;
  for (const auto& algorithm_binding : execution.algorithms()) {
    if (downstream_bindings.find(algorithm_binding.binding_id()) == downstream_bindings.end()) {
      leaf_algorithms.insert(algorithm_binding.binding_id());
    }
  }

  if (execution.outputs_size() == 0) {
    for (const auto& leaf_id : leaf_algorithms) {
      graph.edges.push_back(MakeEdge(
          spec.deployment_id() + ":" + leaf_id + "->" + spec.deployment_id() + ":sink",
          leaf_id,
          "results",
          spec.deployment_id() + ":sink",
          "results",
          "event"));
    }
  } else {
    for (const auto& leaf_id : leaf_algorithms) {
      for (const auto& output_binding : execution.outputs()) {
        const std::string output_node_id =
            output_binding.binding_id().empty() ? spec.deployment_id() + ":sink" : output_binding.binding_id();
        graph.edges.push_back(MakeEdge(
            spec.deployment_id() + ":" + leaf_id + "->" + output_node_id,
            leaf_id,
            "results",
            output_node_id,
            "results",
            "event"));
      }
    }
  }

  return graph;
}

std::string DescribeRecord(const DeploymentRecord& record) {
  std::ostringstream out;
  out << evr::runtime::graph::Describe(record.graph);
  out << "\n  deployment_id=" << record.deployment_id;
  out << " node_id=" << record.node_id;
  out << " state=" << record.state;
  out << " health=" << record.health;
  out << " stopped=" << (record.stopped ? "true" : "false");
  out << " execution_mode=" << ExecutionModeToString(record.effective_execution_mode);
  out << " execution_backend=" << ExecutionBackendToString(record.effective_execution_backend);
  out << "\n  request_id=" << record.request.meta().request_id();
  return out.str();
}

void FillDeploymentStatus(const DeploymentRecord& record,
                          edgevision::runtime::v1::DeploymentStatus* status) {
  status->set_deployment_id(record.deployment_id);
  status->set_node_id(record.node_id);
  status->set_runtime_instance_id(record.runtime_instance_id);
  status->set_state(record.state);
  status->set_health(record.health);
  status->set_status_message(record.status_message);
  status->set_effective_execution_mode(record.effective_execution_mode);
  status->set_effective_execution_backend(record.effective_execution_backend);
}

DeploymentRecord BuildRecord(const SupervisorGrpcServiceConfig& config,
                             const edgevision::runtime::v1::ApplyDeploymentRequest& request) {
  DeploymentRecord record;
  const auto& spec = request.spec();
  const auto& execution = spec.execution();

  record.deployment_id = spec.deployment_id();
  record.node_id = spec.node_id();
  record.runtime_instance_id = "instance:" + spec.deployment_id();
  record.operation_id = "apply:" + spec.deployment_id();
  record.status_message = "deployment accepted and normalized";
  record.state = edgevision::common::v1::LIFECYCLE_STATE_APPLYING;
  record.health = edgevision::common::v1::HEALTH_STATE_OK;
  record.effective_execution_mode = spec.execution_mode() == edgevision::common::v1::EXECUTION_MODE_UNSPECIFIED
                                        ? execution.execution_mode()
                                        : spec.execution_mode();
  record.effective_execution_backend = spec.execution_backend() ==
                                              edgevision::common::v1::EXECUTION_BACKEND_UNSPECIFIED
                                          ? execution.execution_backend()
                                          : spec.execution_backend();
  record.request = request;
  record.graph = BuildExecutionGraph(request, config.node_id, config.runtime_version);

  if (record.node_id.empty()) {
    record.node_id = config.node_id;
  }
  return record;
}

bool HasValidOutputBinding(const edgevision::runtime::v1::RuntimeOutputBinding& binding) {
  return !binding.binding_id().empty() && !binding.sink().kind().empty() && !binding.sink().id().empty();
}

std::string MakeSourceSessionId(const edgevision::runtime::v1::RuntimeSourceBinding& binding) {
  return binding.binding_id();
}

std::string MakeWorkerSessionId(const edgevision::runtime::v1::RuntimeAlgorithmBinding& binding) {
  return binding.binding_id();
}

source::SourceSessionConfig BuildSourceSessionConfig(
    const SupervisorGrpcServiceConfig& config,
    const edgevision::runtime::v1::ApplyDeploymentRequest& request,
    const edgevision::runtime::v1::RuntimeSourceBinding& binding) {
  source::SourceSessionConfig session_config;
  session_config.session_id = MakeSourceSessionId(binding);
  session_config.source_uri =
      "source://" + (binding.input_binding().kind().empty() ? "camera" : binding.input_binding().kind()) +
      "/" + binding.input_binding().id();
  session_config.upstream_kind = binding.input_binding().kind();
  session_config.upstream_endpoint = binding.input_binding().id();
  session_config.transport_protocol =
      binding.input_binding().kind().empty() ? "rtsp" : binding.input_binding().kind();
  session_config.buffer_transport = "host-memory";
  session_config.proto_version = kProtocolVersion;
  session_config.decode_mode = "runtime-dispatch";
  session_config.pixel_format = "rgba";
  session_config.decode_timeout_seconds = 60;
  session_config.decode_log_path = "/tmp/evr_runtime_source_" + request.spec().deployment_id() +
                                   "_" + binding.binding_id() + ".log";
  if (!config.node_id.empty()) {
    session_config.upstream_endpoint = config.node_id + ":" + session_config.upstream_endpoint;
  }
  return session_config;
}

worker::WorkerSessionConfig BuildWorkerSessionConfig(
    const SupervisorGrpcServiceConfig& config,
    const edgevision::runtime::v1::ApplyDeploymentRequest& request,
    const edgevision::runtime::v1::RuntimeAlgorithmBinding& binding) {
  worker::WorkerSessionConfig session_config;
  session_config.session_id = MakeWorkerSessionId(binding);
  session_config.supervisor_endpoint = "runtime://" + config.node_id;
  session_config.source_session_id = binding.input_binding_ids_size() > 0 ? binding.input_binding_ids(0) : "";
  session_config.proto_version = kProtocolVersion;
  session_config.inference_backend = ExecutionBackendToString(binding.execution_backend());
  session_config.engine_path = binding.artifact().id();
  session_config.algorithm_name = binding.algorithm().id();
  session_config.algorithm_package_uri =
      binding.algorithm().kind().empty() ? binding.algorithm().id()
                                         : binding.algorithm().kind() + "://" + binding.algorithm().id();
  session_config.algorithm_entry_point =
      binding.algorithm().kind().empty() ? binding.algorithm().id()
                                         : binding.algorithm().kind() + "::" + binding.algorithm().id();
  session_config.algorithm_runtime_config_uri = binding.config_revision().id();
  session_config.input_binding = binding.input_binding_ids_size() > 0 ? binding.input_binding_ids(0) : "frames";
  session_config.result_encoding = "json";
  session_config.output_topic = request.spec().deployment_id() + "." + binding.binding_id();
  return session_config;
}

bool StartRuntimeState(const SupervisorGrpcServiceConfig& config,
                       const edgevision::runtime::v1::ApplyDeploymentRequest& request,
                       DeploymentRuntimeState* runtime_state,
                       std::string* error) {
  const auto& execution = request.spec().execution();

  runtime_state->source_sessions.clear();
  runtime_state->worker_sessions.clear();
  runtime_state->source_sessions.reserve(static_cast<std::size_t>(execution.sources_size()));
  runtime_state->worker_sessions.reserve(static_cast<std::size_t>(execution.algorithms_size()));

  for (const auto& source_binding : execution.sources()) {
    source::SourceSession session;
    const auto session_config = BuildSourceSessionConfig(config, request, source_binding);
    if (!session.Configure(session_config) || !session.Start()) {
      if (error != nullptr) {
        *error = "source-session-start-failed:" + source_binding.binding_id();
      }
      for (auto& started_worker : runtime_state->worker_sessions) {
        started_worker.Stop();
      }
      for (auto& started_source : runtime_state->source_sessions) {
        started_source.Stop();
      }
      return false;
    }
    runtime_state->source_sessions.push_back(std::move(session));
  }

  for (const auto& algorithm_binding : execution.algorithms()) {
    worker::WorkerSession session;
    const auto session_config = BuildWorkerSessionConfig(config, request, algorithm_binding);
    if (!session.Configure(session_config) || !session.Start()) {
      if (error != nullptr) {
        *error = "worker-session-start-failed:" + algorithm_binding.binding_id();
      }
      for (auto& started_worker : runtime_state->worker_sessions) {
        started_worker.Stop();
      }
      for (auto& started_source : runtime_state->source_sessions) {
        started_source.Stop();
      }
      return false;
    }
    runtime_state->worker_sessions.push_back(std::move(session));
  }

  return true;
}

void StopRuntimeState(DeploymentRuntimeState* runtime_state) {
  if (runtime_state == nullptr) {
    return;
  }
  for (auto& worker_session : runtime_state->worker_sessions) {
    worker_session.Stop();
  }
  for (auto& source_session : runtime_state->source_sessions) {
    source_session.Stop();
  }
  runtime_state->worker_sessions.clear();
  runtime_state->source_sessions.clear();
}

}  // namespace

SupervisorGrpcService::SupervisorGrpcService(SupervisorGrpcServiceConfig config)
    : config_(std::move(config)) {
  if (config_.node_id.empty()) {
    config_.node_id = "runtime-node-1";
  }
  if (config_.runtime_version.empty()) {
    config_.runtime_version = "runtime-cpp-grpc";
  }
  if (config_.max_source_bindings == 0) {
    config_.max_source_bindings = 8;
  }
  if (config_.max_algorithm_bindings == 0) {
    config_.max_algorithm_bindings = 8;
  }
}

grpc::Status SupervisorGrpcService::Handshake(
    grpc::ServerContext*,
    const edgevision::runtime::v1::HandshakeRequest* request,
    edgevision::runtime::v1::HandshakeResponse* response) {
  const std::string protocol =
      request->protocol_version().empty() ? kProtocolVersion : request->protocol_version();
  response->set_protocol_version(kProtocolVersion);
  *response->mutable_capability() = BuildCapability();

  if (protocol != kProtocolVersion) {
    response->set_accepted(false);
    response->set_message("unsupported protocol version: " + request->protocol_version());
    return grpc::Status::OK;
  }

  if (!request->node_id().empty() && request->node_id() != config_.node_id) {
    response->set_accepted(false);
    response->set_message("node mismatch: runtime=" + config_.node_id +
                          " requested=" + request->node_id());
    return grpc::Status::OK;
  }

  const std::string session_id =
      "session:" + request->control_plane_id() + ":" + config_.node_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_by_node_[config_.node_id] = session_id;
  }

  response->set_accepted(true);
  response->set_session_id(session_id);
  response->set_message("accepted");
  return grpc::Status::OK;
}

grpc::Status SupervisorGrpcService::ApplyDeployment(
    grpc::ServerContext*,
    const edgevision::runtime::v1::ApplyDeploymentRequest* request,
    edgevision::runtime::v1::ApplyDeploymentResponse* response) {
  const auto& spec = request->spec();
  if (!HasHandshakeForNode(spec.node_id())) {
    FillRejectedApply(spec.deployment_id(), "handshake-required", response);
    return grpc::Status::OK;
  }

  std::string error;
  if (!ValidateExecutionRequest(*request, &error)) {
    FillRejectedApply(spec.deployment_id(), error, response);
    return grpc::Status::OK;
  }

  DeploymentRecord record = BuildRecord(config_, *request);
  DeploymentRuntimeState runtime_state;
  if (!StartRuntimeState(config_, *request, &runtime_state, &error)) {
    FillRejectedApply(spec.deployment_id(), error.empty() ? "runtime-start-failed" : error, response);
    return grpc::Status::OK;
  }

  record.state = edgevision::common::v1::LIFECYCLE_STATE_RUNNING;
  record.status_message = "deployment running";
  {
    std::lock_guard<std::mutex> lock(mutex_);
    records_by_deployment_[record.deployment_id] = std::move(record);
    runtime_state_by_deployment_[spec.deployment_id()] = std::move(runtime_state);
  }

  response->set_accepted(true);
  response->set_operation_id("apply:" + spec.deployment_id());
  return grpc::Status::OK;
}

grpc::Status SupervisorGrpcService::StopDeployment(
    grpc::ServerContext*,
    const edgevision::runtime::v1::StopDeploymentRequest* request,
    edgevision::runtime::v1::StopDeploymentResponse* response) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto runtime_it = runtime_state_by_deployment_.find(request->deployment_id());
  if (runtime_it != runtime_state_by_deployment_.end()) {
    StopRuntimeState(&runtime_it->second);
    runtime_state_by_deployment_.erase(runtime_it);
  }
  auto& record = records_by_deployment_[request->deployment_id()];
  if (record.deployment_id.empty()) {
    record.deployment_id = request->deployment_id();
    record.node_id = request->node_id().empty() ? config_.node_id : request->node_id();
    record.runtime_instance_id = "instance:" + request->deployment_id();
    record.effective_execution_mode = edgevision::common::v1::EXECUTION_MODE_UNSPECIFIED;
    record.effective_execution_backend = edgevision::common::v1::EXECUTION_BACKEND_UNSPECIFIED;
  }
  record.state = edgevision::common::v1::LIFECYCLE_STATE_STOPPED;
  record.health = edgevision::common::v1::HEALTH_STATE_UNKNOWN;
  record.stopped = true;
  record.status_message = "deployment stopped";
  record.operation_id = "stop:" + request->deployment_id();

  response->set_accepted(true);
  response->set_operation_id(record.operation_id);
  return grpc::Status::OK;
}

grpc::Status SupervisorGrpcService::GetSupervisorStatus(
    grpc::ServerContext*,
    const edgevision::runtime::v1::GetSupervisorStatusRequest* request,
    edgevision::runtime::v1::GetSupervisorStatusResponse* response) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* status = response->mutable_status();
  status->set_node_id(request->node_id().empty() ? config_.node_id : request->node_id());
  status->set_supervisor_version(config_.runtime_version);
  status->set_state(edgevision::common::v1::LIFECYCLE_STATE_RUNNING);
  status->set_health(edgevision::common::v1::HEALTH_STATE_OK);
  std::uint32_t active_count = 0;
  for (const auto& [_, record] : records_by_deployment_) {
    if (!record.stopped && record.state == edgevision::common::v1::LIFECYCLE_STATE_RUNNING) {
      ++active_count;
    }
  }
  status->set_active_deployment_count(active_count);
  status->set_default_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  status->set_default_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);
  return grpc::Status::OK;
}

grpc::Status SupervisorGrpcService::GetDeploymentStatus(
    grpc::ServerContext*,
    const edgevision::runtime::v1::GetDeploymentStatusRequest* request,
    edgevision::runtime::v1::GetDeploymentStatusResponse* response) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* status = response->mutable_status();
  status->set_deployment_id(request->deployment_id());
  status->set_node_id(request->node_id().empty() ? config_.node_id : request->node_id());

  const auto it = records_by_deployment_.find(request->deployment_id());
  if (it == records_by_deployment_.end()) {
    status->set_runtime_instance_id("instance:" + request->deployment_id());
    status->set_state(edgevision::common::v1::LIFECYCLE_STATE_PENDING);
    status->set_health(edgevision::common::v1::HEALTH_STATE_UNKNOWN);
    status->set_status_message("deployment not found");
    status->set_effective_execution_mode(edgevision::common::v1::EXECUTION_MODE_UNSPECIFIED);
    status->set_effective_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_UNSPECIFIED);
    return grpc::Status::OK;
  }

  FillDeploymentStatus(it->second, status);
  return grpc::Status::OK;
}

bool SupervisorGrpcService::HasDeployment(const std::string& deployment_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return records_by_deployment_.find(deployment_id) != records_by_deployment_.end();
}

std::string SupervisorGrpcService::DescribeDeployment(const std::string& deployment_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = records_by_deployment_.find(deployment_id);
  if (it == records_by_deployment_.end()) {
    return {};
  }
  std::string description = DescribeRecord(it->second);
  const auto runtime_it = runtime_state_by_deployment_.find(deployment_id);
  if (runtime_it != runtime_state_by_deployment_.end()) {
    description += "\n  source_sessions=" + std::to_string(runtime_it->second.source_sessions.size());
    description += " worker_sessions=" + std::to_string(runtime_it->second.worker_sessions.size());
    for (const auto& source_session : runtime_it->second.source_sessions) {
      const auto snapshot = source_session.GetSnapshot();
      description += "\n    source_session[" + snapshot.session_id + "] " + snapshot.detail;
    }
    for (const auto& worker_session : runtime_it->second.worker_sessions) {
      const auto snapshot = worker_session.GetSnapshot();
      description += "\n    worker_session[" + snapshot.session_id + "] " + snapshot.detail;
    }
  }
  return description;
}

edgevision::runtime::v1::RuntimeCapability SupervisorGrpcService::BuildCapability() const {
  edgevision::runtime::v1::RuntimeCapability capability;
  capability.set_node_id(config_.node_id);
  capability.set_runtime_version(config_.runtime_version);
  capability.add_supported_execution_modes(edgevision::common::v1::EXECUTION_MODE_LOCAL_FRAME);
  capability.add_supported_execution_modes(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  capability.add_supported_execution_backends(edgevision::common::v1::EXECUTION_BACKEND_CPU);
  capability.add_supported_execution_backends(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);
  capability.set_max_source_bindings(config_.max_source_bindings);
  capability.set_max_algorithm_bindings(config_.max_algorithm_bindings);
  capability.set_supports_event_stream(true);
  (*capability.mutable_labels())["runtime_component"] = "cpp-supervisor-grpc";
  return capability;
}

bool SupervisorGrpcService::HasHandshakeForNode(const std::string& node_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_by_node_.find(node_id) != sessions_by_node_.end();
}

bool SupervisorGrpcService::ValidateExecutionRequest(
    const edgevision::runtime::v1::ApplyDeploymentRequest& request,
    std::string* error) const {
  const auto& spec = request.spec();
  const auto& execution = spec.execution();

  if (spec.deployment_id().empty()) {
    *error = "deployment-id-required";
    return false;
  }
  if (spec.node_id().empty()) {
    *error = "node-id-required";
    return false;
  }
  if (spec.node_id() != config_.node_id) {
    *error = "node-mismatch";
    return false;
  }
  if (!execution.execution_request_id().empty() &&
      execution.execution_request_id() != spec.deployment_id()) {
    *error = "execution-request-id-mismatch";
    return false;
  }
  if (execution.sources().empty()) {
    *error = "sources-required";
    return false;
  }
  if (static_cast<std::uint32_t>(execution.sources_size()) > config_.max_source_bindings) {
    *error = "too-many-sources";
    return false;
  }
  if (execution.algorithms().empty()) {
    *error = "algorithms-required";
    return false;
  }
  if (static_cast<std::uint32_t>(execution.algorithms_size()) > config_.max_algorithm_bindings) {
    *error = "too-many-algorithms";
    return false;
  }

  std::set<std::string> source_ids;
  for (const auto& source : execution.sources()) {
    if (source.binding_id().empty() || source.input_binding().kind().empty() ||
        source.input_binding().id().empty()) {
      *error = "invalid-source-binding";
      return false;
    }
    source_ids.insert(source.binding_id());
  }

  std::set<std::string> available_inputs = source_ids;
  for (const auto& algorithm : execution.algorithms()) {
    if (algorithm.binding_id().empty() || algorithm.algorithm().kind().empty() ||
        algorithm.algorithm().id().empty() || algorithm.artifact().kind().empty() ||
        algorithm.artifact().id().empty() || algorithm.config_revision().kind().empty() ||
        algorithm.config_revision().id().empty()) {
      *error = "invalid-algorithm-binding";
      return false;
    }
    if (available_inputs.find(algorithm.binding_id()) != available_inputs.end()) {
      *error = "duplicate-binding-id";
      return false;
    }
    if (algorithm.input_binding_ids().empty()) {
      *error = "algorithm-inputs-required";
      return false;
    }
    for (const auto& input_id : algorithm.input_binding_ids()) {
      if (available_inputs.find(input_id) == available_inputs.end()) {
        *error = "unknown-algorithm-input";
        return false;
      }
    }
    available_inputs.insert(algorithm.binding_id());
  }

  for (const auto& output : execution.outputs()) {
    if (!HasValidOutputBinding(output)) {
      *error = "invalid-output-binding";
      return false;
    }
  }

  return true;
}

}  // namespace evr::runtime::supervisor
