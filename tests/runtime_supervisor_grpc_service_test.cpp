#include "evr/runtime/supervisor/supervisor_grpc_service.h"

#include <cassert>
#include <iostream>

namespace {

edgevision::runtime::v1::ApplyDeploymentRequest BuildMultiStreamRequest() {
  edgevision::runtime::v1::ApplyDeploymentRequest request;
  auto* spec = request.mutable_spec();
  spec->set_deployment_id("dep-1");
  spec->set_node_id("node-1");
  spec->set_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  spec->set_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);

  auto* execution = spec->mutable_execution();
  execution->set_execution_request_id("dep-1");
  execution->set_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  execution->set_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);

  auto* source1 = execution->add_sources();
  source1->set_binding_id("source:camera-1");
  source1->mutable_input_binding()->set_kind("camera");
  source1->mutable_input_binding()->set_id("camera-1");

  auto* source2 = execution->add_sources();
  source2->set_binding_id("source:camera-2");
  source2->mutable_input_binding()->set_kind("camera");
  source2->mutable_input_binding()->set_id("camera-2");

  auto* algorithm = execution->add_algorithms();
  algorithm->set_binding_id("algorithm:person-detect");
  algorithm->mutable_algorithm()->set_kind("algorithm");
  algorithm->mutable_algorithm()->set_id("person-detect");
  algorithm->mutable_artifact()->set_kind("algorithm");
  algorithm->mutable_artifact()->set_id("person-detect");
  algorithm->mutable_config_revision()->set_kind("config-revision");
  algorithm->mutable_config_revision()->set_id("rev-1");
  algorithm->set_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  algorithm->set_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);
  algorithm->add_input_binding_ids("source:camera-1");
  algorithm->add_input_binding_ids("source:camera-2");

  auto* algorithm2 = execution->add_algorithms();
  algorithm2->set_binding_id("algorithm:person-track");
  algorithm2->mutable_algorithm()->set_kind("algorithm");
  algorithm2->mutable_algorithm()->set_id("person-track");
  algorithm2->mutable_artifact()->set_kind("algorithm");
  algorithm2->mutable_artifact()->set_id("person-track");
  algorithm2->mutable_config_revision()->set_kind("config-revision");
  algorithm2->mutable_config_revision()->set_id("rev-1");
  algorithm2->set_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  algorithm2->set_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);
  algorithm2->add_input_binding_ids("algorithm:person-detect");

  auto* output = execution->add_outputs();
  output->set_binding_id("output:event-stream");
  output->set_kind("event-topic");
  output->mutable_sink()->set_kind("topic");
  output->mutable_sink()->set_id("events.detection");

  return request;
}

}  // namespace

int main() {
  evr::runtime::supervisor::SupervisorGrpcServiceConfig config;
  config.node_id = "node-1";
  config.runtime_version = "test-runtime";
  config.max_source_bindings = 4;
  config.max_algorithm_bindings = 4;
  evr::runtime::supervisor::SupervisorGrpcService service(config);

  edgevision::runtime::v1::ApplyDeploymentResponse rejected_before_handshake;
  const auto request = BuildMultiStreamRequest();
  assert(service.ApplyDeployment(nullptr, &request, &rejected_before_handshake).ok());
  assert(!rejected_before_handshake.accepted());

  edgevision::runtime::v1::HandshakeRequest handshake_request;
  handshake_request.set_control_plane_id("control-plane-1");
  handshake_request.set_node_id("node-1");
  handshake_request.set_protocol_version("runtime.v1");
  handshake_request.set_desired_execution_mode(edgevision::common::v1::EXECUTION_MODE_REMOTE_GRPC);
  handshake_request.set_desired_execution_backend(edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);

  edgevision::runtime::v1::HandshakeResponse handshake_response;
  assert(service.Handshake(nullptr, &handshake_request, &handshake_response).ok());
  assert(handshake_response.accepted());
  assert(handshake_response.capability().max_source_bindings() == 4);

  edgevision::runtime::v1::ApplyDeploymentResponse apply_response;
  assert(service.ApplyDeployment(nullptr, &request, &apply_response).ok());
  assert(apply_response.accepted());
  assert(apply_response.operation_id() == "apply:dep-1");
  assert(service.HasDeployment("dep-1"));
  const auto summary = service.DescribeDeployment("dep-1");
  assert(summary.find("graph[dep-1]") != std::string::npos);
  assert(summary.find("source:camera-1") != std::string::npos);
  assert(summary.find("source:camera-2") != std::string::npos);
  assert(summary.find("algorithm:person-track") != std::string::npos);
  assert(summary.find("output:event-stream") != std::string::npos);
  assert(summary.find("source_sessions=2") != std::string::npos);
  assert(summary.find("worker_sessions=2") != std::string::npos);

  edgevision::runtime::v1::GetDeploymentStatusRequest status_request;
  status_request.set_deployment_id("dep-1");
  status_request.set_node_id("node-1");
  edgevision::runtime::v1::GetDeploymentStatusResponse status_response;
  assert(service.GetDeploymentStatus(nullptr, &status_request, &status_response).ok());
  assert(status_response.status().state() == edgevision::common::v1::LIFECYCLE_STATE_RUNNING);
  assert(status_response.status().health() == edgevision::common::v1::HEALTH_STATE_OK);
  assert(status_response.status().effective_execution_backend() ==
         edgevision::common::v1::EXECUTION_BACKEND_TENSORRT);

  edgevision::runtime::v1::StopDeploymentRequest stop_request;
  stop_request.set_deployment_id("dep-1");
  stop_request.set_node_id("node-1");
  edgevision::runtime::v1::StopDeploymentResponse stop_response;
  assert(service.StopDeployment(nullptr, &stop_request, &stop_response).ok());
  assert(stop_response.accepted());
  assert(service.HasDeployment("dep-1"));

  edgevision::runtime::v1::GetDeploymentStatusResponse stopped_status_response;
  assert(service.GetDeploymentStatus(nullptr, &status_request, &stopped_status_response).ok());
  assert(stopped_status_response.status().state() == edgevision::common::v1::LIFECYCLE_STATE_STOPPED);

  std::cout << "runtime supervisor grpc service smoke test passed\n";
  return 0;
}
