#include "evr/runtime/deployment/phase1_deployment.h"

#include <utility>

namespace evr::runtime::deployment {
namespace {

bool Fail(std::string message, std::string* error) {
  if (error != nullptr) {
    *error = std::move(message);
  }
  return false;
}

}  // namespace

bool Phase1DeploymentSpec::Normalize(std::string* error) {
  if (deployment_id.empty()) {
    deployment_id = "phase1-demo";
  }

  if (supervisor.session_id.empty()) {
    return Fail("supervisor session_id is required", error);
  }

  if (supervisor.control_endpoint.empty()) {
    return Fail("supervisor control_endpoint is required", error);
  }

  if (source.session_id.empty()) {
    return Fail("source session_id is required", error);
  }

  if (worker.session_id.empty()) {
    return Fail("worker session_id is required", error);
  }

  if (supervisor.proto_version.empty()) {
    supervisor.proto_version = "v1";
  }

  if (source.proto_version.empty()) {
    source.proto_version = supervisor.proto_version;
  }

  if (worker.proto_version.empty()) {
    worker.proto_version = supervisor.proto_version;
  }

  if (source.proto_version != supervisor.proto_version ||
      worker.proto_version != supervisor.proto_version) {
    return Fail("phase-1 deployment expects one proto_version across supervisor/source/worker", error);
  }

  if (worker.source_session_id.empty()) {
    worker.source_session_id = source.session_id;
  } else if (worker.source_session_id != source.session_id) {
    return Fail("worker.source_session_id must match source.session_id", error);
  }

  if (worker.supervisor_endpoint.empty()) {
    worker.supervisor_endpoint = supervisor.control_endpoint;
  } else if (worker.supervisor_endpoint != supervisor.control_endpoint) {
    return Fail("worker.supervisor_endpoint must match supervisor.control_endpoint", error);
  }

  return true;
}

graph::Graph Phase1DeploymentSpec::ToGraph() const {
  graph::Graph graph;
  graph.id = deployment_id;

  graph::Node supervisor_node;
  supervisor_node.id = supervisor.session_id;
  supervisor_node.type = "control";
  supervisor_node.subtype = "supervisor";
  supervisor_node.name = "runtime-supervisor";
  supervisor_node.outputs = {"control", "status"};
  supervisor_node.config_ref = supervisor.control_endpoint;

  graph::Node source_node;
  source_node.id = source.session_id;
  source_node.type = "media";
  source_node.subtype = "source";
  source_node.name = "video-source";
  source_node.outputs = {"frames"};
  source_node.config_ref = "{\"upstream_kind\":\"" + source.upstream_kind +
                           "\",\"upstream_endpoint\":\"" + source.upstream_endpoint +
                           "\",\"source_uri\":\"" + source.source_uri +
                           "\",\"transport_protocol\":\"" + source.transport_protocol +
                           "\",\"buffer_transport\":\"" + source.buffer_transport +
                           "\",\"pixel_format\":\"" + source.pixel_format + "\"}";

  graph::Node worker_node;
  worker_node.id = worker.session_id;
  worker_node.type = "inference";
  worker_node.subtype = worker.inference_backend;
  worker_node.name = "ai-worker";
  worker_node.inputs = {"frames"};
  worker_node.outputs = {"results"};
  worker_node.config_ref = "{\"algorithm_name\":\"" + worker.algorithm_name +
                           "\",\"engine_path\":\"" + worker.engine_path +
                           "\",\"input_binding\":\"" + worker.input_binding +
                           "\",\"result_encoding\":\"" + worker.result_encoding +
                           "\",\"output_topic\":\"" + worker.output_topic +
                           "\",\"backend\":\"" + worker.inference_backend + "\"}";

  graph::Node sink_node;
  sink_node.id = deployment_id + ":sink";
  sink_node.type = "output";
  sink_node.subtype = "event-topic";
  sink_node.name = "result-sink";
  sink_node.inputs = {"results"};
  sink_node.config_ref = "{\"output_topic\":\"" + worker.output_topic + "\"}";

  graph.nodes = {supervisor_node, source_node, worker_node, sink_node};

  graph::Edge source_to_worker;
  source_to_worker.id = deployment_id + ":source->worker";
  source_to_worker.from_node = source.session_id;
  source_to_worker.from_port = "frames";
  source_to_worker.to_node = worker.session_id;
  source_to_worker.to_port = "frames";
  source_to_worker.type = "frame";

  graph::Edge supervisor_to_worker;
  supervisor_to_worker.id = deployment_id + ":supervisor->worker";
  supervisor_to_worker.from_node = supervisor.session_id;
  supervisor_to_worker.from_port = "control";
  supervisor_to_worker.to_node = worker.session_id;
  supervisor_to_worker.to_port = "control";
  supervisor_to_worker.type = "control";

  graph::Edge worker_to_sink;
  worker_to_sink.id = deployment_id + ":worker->sink";
  worker_to_sink.from_node = worker.session_id;
  worker_to_sink.from_port = "results";
  worker_to_sink.to_node = sink_node.id;
  worker_to_sink.to_port = "results";
  worker_to_sink.type = "event";

  graph.edges = {source_to_worker, supervisor_to_worker, worker_to_sink};
  return graph;
}

std::string Phase1DeploymentSpec::DescribeWiring() const {
  return graph::Describe(ToGraph());
}

bool DeploymentController::Apply(Phase1DeploymentSpec spec, std::string* error) {
  if (!spec.Normalize(error)) {
    return false;
  }

  applied_ = true;
  spec_ = std::move(spec);
  status_.deployment_id = spec_.deployment_id;
  status_.wiring = spec_.DescribeWiring();
  status_.state = session::State::kConfigured;
  status_.detail =
      "apply placeholder only; supervisor owns desired topology, runtime IPC/inference not attached yet";
  return true;
}

DeploymentStatus DeploymentController::GetStatus() const {
  if (!applied_) {
    DeploymentStatus status;
    status.deployment_id = "unapplied";
    status.state = session::State::kCreated;
    status.detail = "no deployment has been applied";
    return status;
  }

  return status_;
}

const Phase1DeploymentSpec* DeploymentController::spec() const {
  if (!applied_) {
    return nullptr;
  }

  return &spec_;
}

}  // namespace evr::runtime::deployment
