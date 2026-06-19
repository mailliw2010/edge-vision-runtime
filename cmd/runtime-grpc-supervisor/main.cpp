#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "evr/runtime/supervisor/supervisor_grpc_service.h"

namespace {

std::string FlagValue(const std::string& argument, const std::string& name) {
  const std::string prefix = name + "=";
  if (argument.rfind(prefix, 0) == 0) {
    return argument.substr(prefix.size());
  }
  return "";
}

}  // namespace

int main(int argc, char** argv) {
  std::string listen = "0.0.0.0:19090";
  evr::runtime::supervisor::SupervisorGrpcServiceConfig config;

  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      std::cout << "Usage: runtime-grpc-supervisor [--listen=HOST:PORT] [--node-id=ID]\n";
      return 0;
    }
    if (const std::string value = FlagValue(argument, "--listen"); !value.empty()) {
      listen = value;
      continue;
    }
    if (const std::string value = FlagValue(argument, "--node-id"); !value.empty()) {
      config.node_id = value;
      continue;
    }
    std::cerr << "unknown argument: " << argument << '\n';
    return 1;
  }

  evr::runtime::supervisor::SupervisorGrpcService service(config);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(listen, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::cerr << "failed to start runtime gRPC supervisor on " << listen << '\n';
    return 1;
  }

  std::cout << "runtime gRPC supervisor listening on " << listen
            << " node_id=" << config.node_id << '\n';
  server->Wait();
  return 0;
}
