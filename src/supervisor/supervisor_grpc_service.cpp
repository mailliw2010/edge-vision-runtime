#include "evr/runtime/supervisor/supervisor_grpc_service.h"

#include <set>
#include <utility>

namespace evr::runtime::supervisor {
namespace {

constexpr const char* kProtocolVersion = "runtime.v1";

void FillRejectedApply(const std::string& deployment_id,
                       const std::string& reason,
                       edgevision::runtime::v1::ApplyDeploymentResponse* response) {
  response->set_accepted(false);
  response->set_operation_id("rejected:" + deployment_id + ":" + reason);
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
    config_.max_algorithm_bindings = 1;
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

  {
    std::lock_guard<std::mutex> lock(mutex_);
    deployments_[spec.deployment_id()] = *request;
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
  deployments_.erase(request->deployment_id());
  response->set_accepted(true);
  response->set_operation_id("stop:" + request->deployment_id());
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
  status->set_active_deployment_count(static_cast<std::uint32_t>(deployments_.size()));
  status->set_default_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  status->set_default_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);
  return grpc::Status::OK;
}

grpc::Status SupervisorGrpcService::GetDeploymentStatus(
    grpc::ServerContext*,
    const edgevision::runtime::v1::GetDeploymentStatusRequest* request,
    edgevision::runtime::v1::GetDeploymentStatusResponse* response) {
  const bool found = HasDeployment(request->deployment_id());
  auto* status = response->mutable_status();
  status->set_deployment_id(request->deployment_id());
  status->set_node_id(request->node_id().empty() ? config_.node_id : request->node_id());
  status->set_runtime_instance_id("instance:" + request->deployment_id());
  status->set_state(found ? edgevision::common::v1::LIFECYCLE_STATE_RUNNING
                          : edgevision::common::v1::LIFECYCLE_STATE_PENDING);
  status->set_health(found ? edgevision::common::v1::HEALTH_STATE_OK
                           : edgevision::common::v1::HEALTH_STATE_UNKNOWN);
  status->set_status_message(found ? "deployment accepted" : "deployment not found");
  status->set_effective_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  status->set_effective_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);
  return grpc::Status::OK;
}

bool SupervisorGrpcService::HasDeployment(const std::string& deployment_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return deployments_.find(deployment_id) != deployments_.end();
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

  if (spec.node_id() != config_.node_id) {
    *error = "node-mismatch";
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
  if (execution.algorithms_size() != 1 ||
      static_cast<std::uint32_t>(execution.algorithms_size()) > config_.max_algorithm_bindings) {
    *error = "expected-one-algorithm";
    return false;
  }

  std::set<std::string> source_ids;
  for (const auto& source : execution.sources()) {
    if (source.binding_id().empty() || source.input_binding().id().empty()) {
      *error = "invalid-source-binding";
      return false;
    }
    source_ids.insert(source.binding_id());
  }

  const auto& algorithm = execution.algorithms(0);
  if (algorithm.input_binding_ids_size() != execution.sources_size()) {
    *error = "algorithm-input-count-mismatch";
    return false;
  }
  for (const auto& input_id : algorithm.input_binding_ids()) {
    if (source_ids.find(input_id) == source_ids.end()) {
      *error = "unknown-algorithm-input";
      return false;
    }
  }

  return true;
}

}  // namespace evr::runtime::supervisor
