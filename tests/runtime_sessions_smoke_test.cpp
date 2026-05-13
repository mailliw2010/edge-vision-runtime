#include <cassert>
#include <filesystem>
#include <string>

#include "evr/runtime/config/runtime_config_loader.h"
#include "evr/runtime/deployment/phase1_deployment.h"
#include "evr/runtime/graph/graph.h"
#include "evr/runtime/source/source_session.h"
#include "evr/runtime/supervisor/supervisor_session.h"
#include "evr/runtime/worker/worker_session.h"

int main() {
  namespace fs = std::filesystem;
  using evr::runtime::session::State;

  evr::runtime::supervisor::SupervisorSession supervisor_session;
  evr::runtime::supervisor::SupervisorSessionConfig supervisor_config;
  supervisor_config.session_id = "supervisor-test";
  supervisor_config.control_endpoint = "unix:///tmp/test-supervisor.sock";
  supervisor_config.proto_version = "v1";
  assert(supervisor_session.Configure(supervisor_config));
  assert(supervisor_session.Start());
  assert(supervisor_session.GetSnapshot().state == State::kRunning);
  supervisor_session.Stop();
  assert(supervisor_session.state() == State::kStopped);

  evr::runtime::source::SourceSession source_session;
  evr::runtime::source::SourceSessionConfig source_config;
  source_config.session_id = "source-test";
  source_config.upstream_kind = "zlm-proxy";
  source_config.upstream_endpoint = "rtsp://127.0.0.1:554/camera-0";
  source_config.transport_protocol = "rtsp";
  source_config.buffer_transport = "nvmm";
  source_config.proto_version = "v1";
  source_config.decode_mode = "jetson-nvdec";
  source_config.pixel_format = "nv12";
  source_config.decode_timeout_seconds = 60;
  source_config.decode_log_path = "/tmp/test-source-ffmpeg.log";
  assert(source_session.Configure(source_config));
  assert(source_session.Start());
  assert(source_session.GetSnapshot().state == State::kRunning);
  source_session.Stop();
  assert(source_session.state() == State::kStopped);

  evr::runtime::worker::WorkerSession worker_session;
  evr::runtime::worker::WorkerSessionConfig worker_config;
  worker_config.session_id = "worker-test";
  worker_config.supervisor_endpoint = "unix:///tmp/test-supervisor.sock";
  worker_config.source_session_id = "source-test";
  worker_config.proto_version = "v1";
  worker_config.inference_backend = "tensorrt";
  worker_config.engine_path = "models/yolov8s.onnx";
  worker_config.algorithm_name = "yolov8-person-detection";
  worker_config.algorithm_package_uri = "algorithm/yolov8_person_detection";
  worker_config.algorithm_entry_point = "evr::algorithm::yolov8_person_detection::YoloV8PersonDetector";
  worker_config.algorithm_runtime_config_uri = "algorithm/yolov8_person_detection/configs/yolov8_person_detection.v1.example.yaml";
  worker_config.input_binding = "frames";
  worker_config.result_encoding = "json";
  worker_config.output_topic = "events.test";
  assert(worker_session.Configure(worker_config));
  assert(worker_session.Start());
  assert(worker_session.GetSnapshot().state == State::kRunning);
  worker_session.Stop();
  assert(worker_session.state() == State::kStopped);

  evr::runtime::deployment::Phase1DeploymentSpec spec;
  spec.deployment_id = "phase1-test";
  spec.supervisor = supervisor_config;
  spec.source = source_config;
  spec.worker = worker_config;
  spec.worker.source_session_id.clear();
  spec.worker.supervisor_endpoint.clear();

  std::string error;
  assert(spec.Normalize(&error));
  assert(spec.worker.source_session_id == spec.source.session_id);
  assert(spec.worker.supervisor_endpoint == spec.supervisor.control_endpoint);
  assert(spec.DescribeWiring().find("edge[phase1-test:source->worker]") != std::string::npos);

  const auto graph = spec.ToGraph();
  assert(graph.nodes.size() == 4);
  assert(graph.edges.size() == 3);
  const auto json = evr::runtime::graph::ToJson(graph);
  assert(json.find("\"id\":\"phase1-test\"") != std::string::npos);
  assert(json.find("\"type\":\"media\"") != std::string::npos);
  assert(json.find("\"subtype\":\"source\"") != std::string::npos);
  assert(json.find("events.test") != std::string::npos);
  assert(json.find("\"type\":\"output\"") != std::string::npos);

  evr::runtime::deployment::DeploymentController deployment_controller;
  assert(deployment_controller.Apply(spec, &error));
  const auto status = deployment_controller.GetStatus();
  assert(status.deployment_id == "phase1-test");
  assert(status.state == State::kConfigured);
  assert(status.wiring.find("graph[phase1-test]") != std::string::npos);

  const fs::path project_root = fs::path(__FILE__).parent_path().parent_path();
  evr::runtime::config::RuntimeConfigLoader loader;

  evr::runtime::source::SourceAppConfig loaded_source_config;
  assert(loader.LoadSourceAppConfig((project_root / "configs/runtime-source.v1.example.yaml").string(),
                                    &loaded_source_config, &error));
  assert(loaded_source_config.session.session_id == "source-demo");
  assert(loaded_source_config.session.decode_mode == "jetson-nvdec");
  assert(loaded_source_config.session.transport_protocol == "rtsp");
  assert(loaded_source_config.session.buffer_transport == "nvmm");
  assert(loaded_source_config.session.pixel_format == "nv12");
  assert(loaded_source_config.session.decode_timeout_seconds == 60);
  assert(loaded_source_config.session.decode_log_path == "/tmp/evr_runtime_source_ffmpeg.log");

  evr::runtime::supervisor::SupervisorAppConfig loaded_supervisor_config;
  assert(loader.LoadSupervisorAppConfig(
      (project_root / "configs/runtime-supervisor.v1.example.yaml").string(),
      &loaded_supervisor_config, &error));
  assert(loaded_supervisor_config.session.session_id == "supervisor-main");
  assert(loaded_supervisor_config.deployment.supervisor.control_endpoint ==
         loaded_supervisor_config.session.control_endpoint);

  evr::runtime::deployment::Phase1DeploymentSpec loaded_deployment_spec;
  assert(loader.LoadPhase1DeploymentSpec(
      (project_root / "configs/runtime-phase1-deployment.v1.example.yaml").string(),
      &loaded_deployment_spec, &error));
  assert(loaded_deployment_spec.deployment_id == "phase1-demo");
  assert(loaded_deployment_spec.source.session_id == "source-demo");
  assert(loaded_deployment_spec.worker.session_id == "worker-0");
  assert(loaded_deployment_spec.Normalize(&error));
  assert(loaded_deployment_spec.worker.source_session_id == loaded_deployment_spec.source.session_id);
  assert(loaded_deployment_spec.worker.supervisor_endpoint ==
         loaded_deployment_spec.supervisor.control_endpoint);

  evr::runtime::worker::WorkerAppConfig loaded_worker_config;
  assert(loader.LoadWorkerAppConfig((project_root / "configs/runtime-worker.v1.example.yaml").string(),
                                    &loaded_worker_config, &error));
  assert(loaded_worker_config.source.session_id == "source-demo");
  assert(loaded_worker_config.worker.session_id == "worker-0");
  assert(loaded_worker_config.worker.algorithm_name == "yolov8-person-detection");
  assert(loaded_worker_config.worker.algorithm_package_uri == "algorithm/yolov8_person_detection");
  assert(loaded_worker_config.worker.algorithm_entry_point == "evr::algorithm::yolov8_person_detection::YoloV8PersonDetector");
  assert(loaded_worker_config.worker.algorithm_runtime_config_uri == "algorithm/yolov8_person_detection/configs/yolov8_person_detection.v1.example.yaml");
  assert(loaded_worker_config.worker.input_binding == "frames");
  assert(loaded_worker_config.worker.result_encoding == "json");
  assert(loaded_worker_config.worker.output_topic == "events.detection");
  assert(loaded_worker_config.worker.supervisor_endpoint ==
         "unix:///tmp/evr-supervisor.sock");

  return 0;
}
