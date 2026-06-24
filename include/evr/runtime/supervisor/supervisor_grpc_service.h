#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "evr/runtime/graph/graph.h"
#include "evr/runtime/source/source_session.h"
#include "evr/runtime/worker/worker_session.h"
#include "runtime/v1/runtime.grpc.pb.h"

namespace evr::runtime::supervisor {

struct SupervisorGrpcServiceConfig {
  std::string node_id{"runtime-node-1"};
  std::string runtime_version{"runtime-cpp-grpc"};
  std::uint32_t max_source_bindings{8};
  std::uint32_t max_algorithm_bindings{8};
};

struct DeploymentRecord {
  std::string deployment_id;
  std::string node_id;
  std::string runtime_instance_id;
  std::string operation_id;
  std::string status_message;
  edgevision::common::v1::LifecycleState state{edgevision::common::v1::LIFECYCLE_STATE_PENDING};
  edgevision::common::v1::HealthState health{edgevision::common::v1::HEALTH_STATE_UNKNOWN};
  edgevision::common::v1::ExecutionMode effective_execution_mode{
      edgevision::common::v1::EXECUTION_MODE_UNSPECIFIED};
  edgevision::common::v1::ExecutionBackend effective_execution_backend{
      edgevision::common::v1::EXECUTION_BACKEND_UNSPECIFIED};
  edgevision::runtime::v1::ApplyDeploymentRequest request;
  evr::runtime::graph::Graph graph;
  bool stopped{false};
};

struct DeploymentRuntimeState {
  std::vector<evr::runtime::source::SourceSession> source_sessions;
  std::vector<evr::runtime::worker::WorkerSession> worker_sessions;
};

class SupervisorGrpcService final : public edgevision::runtime::v1::SupervisorService::Service {
 public:
  explicit SupervisorGrpcService(SupervisorGrpcServiceConfig config = {});

  grpc::Status Handshake(
      grpc::ServerContext* context,
      const edgevision::runtime::v1::HandshakeRequest* request,
      edgevision::runtime::v1::HandshakeResponse* response) override;

  grpc::Status ApplyDeployment(
      grpc::ServerContext* context,
      const edgevision::runtime::v1::ApplyDeploymentRequest* request,
      edgevision::runtime::v1::ApplyDeploymentResponse* response) override;

  grpc::Status StopDeployment(
      grpc::ServerContext* context,
      const edgevision::runtime::v1::StopDeploymentRequest* request,
      edgevision::runtime::v1::StopDeploymentResponse* response) override;

  grpc::Status GetSupervisorStatus(
      grpc::ServerContext* context,
      const edgevision::runtime::v1::GetSupervisorStatusRequest* request,
      edgevision::runtime::v1::GetSupervisorStatusResponse* response) override;

  grpc::Status GetDeploymentStatus(
      grpc::ServerContext* context,
      const edgevision::runtime::v1::GetDeploymentStatusRequest* request,
      edgevision::runtime::v1::GetDeploymentStatusResponse* response) override;

  bool HasDeployment(const std::string& deployment_id) const;
  std::string DescribeDeployment(const std::string& deployment_id) const;

 private:
  edgevision::runtime::v1::RuntimeCapability BuildCapability() const;
  bool HasHandshakeForNode(const std::string& node_id) const;
  bool ValidateExecutionRequest(
      const edgevision::runtime::v1::ApplyDeploymentRequest& request,
      std::string* error) const;

  SupervisorGrpcServiceConfig config_{};
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::string> sessions_by_node_;
  std::unordered_map<std::string, DeploymentRecord> records_by_deployment_;
  std::unordered_map<std::string, DeploymentRuntimeState> runtime_state_by_deployment_;
};

}  // namespace evr::runtime::supervisor
