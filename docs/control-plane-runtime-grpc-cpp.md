# control-plane -> C++ runtime gRPC control surface

Runtime directly implements the gRPC control surface in C++.

```text
control-plane (Go)
  -> runtime.v1.SupervisorService over gRPC
runtime-supervisor (C++)
  -> SourceSession / WorkerSession / Graph / GStreamer / TensorRT
```

There is no Go runtime supervisor shim and no extra Go -> C++ internal control protocol.

## RPCs

The contract is defined in `edge-vision-contracts/proto/runtime/v1/runtime.proto`.

Runtime implements:

- `Handshake`
- `ApplyDeployment`
- `StopDeployment`
- `GetSupervisorStatus`
- `GetDeploymentStatus`

`Handshake` negotiates protocol version and exposes runtime capability. `ApplyDeployment` accepts the declarative `ExecutionRequest`; for the multi-stream example, it contains several `RuntimeSourceBinding` entries and one `RuntimeAlgorithmBinding` whose `input_binding_ids` reference all sources.

## Build

The C++ gRPC surface is optional so the default runtime build does not require gRPC C++ packages.

```bash
cmake -S . -B build-grpc -DEVR_ENABLE_GRPC=ON
cmake --build build-grpc --target runtime-grpc-supervisor runtime_supervisor_grpc_service_test
ctest --test-dir build-grpc --output-on-failure -R runtime_supervisor_grpc_service_test
```

When enabled, CMake generates C++ protobuf/gRPC files from:

```text
../edge-vision-contracts/proto/common/v1/common.proto
../edge-vision-contracts/proto/runtime/v1/runtime.proto
```

Generated files stay under the build directory and are not committed.

## Run

```bash
./build-grpc/bin/runtime-grpc-supervisor --listen=0.0.0.0:19090 --node-id=node-1
```

control-plane should point to this endpoint:

```bash
EDGEVISION_RUNTIME_MODE=grpc
EDGEVISION_RUNTIME_ENDPOINT=127.0.0.1:19090
```

## Current Scope

The initial C++ implementation validates the gRPC boundary and stores accepted deployment requests in memory:

- reject `ApplyDeployment` before `Handshake`
- reject mismatched `node_id`
- require at least one source
- require exactly one algorithm binding
- require the algorithm binding to reference all source bindings

The next step is mapping `ApplyDeploymentRequest.ExecutionRequest` into `Phase1DeploymentSpec` and then into the real source/worker/graph lifecycle.

