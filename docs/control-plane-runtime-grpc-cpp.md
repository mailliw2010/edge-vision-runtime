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

`Handshake` negotiates protocol version and exposes runtime capability. `ApplyDeployment` accepts the declarative `ExecutionRequest`; for the multi-source / multi-algorithm example, it contains several `RuntimeSourceBinding` entries and a DAG of `RuntimeAlgorithmBinding` entries whose `input_binding_ids` reference source bindings or upstream algorithm bindings.

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

The current C++ implementation validates the gRPC boundary, normalizes the declarative request into an internal execution graph, and stores accepted deployment state in memory:

- reject `ApplyDeployment` before `Handshake`
- reject mismatched `node_id`
- require at least one source
- require at least one algorithm binding
- require every algorithm binding to reference only declared sources or upstream algorithm bindings
- normalize the accepted request into a graph with supervisor/source/algorithm/output nodes
- expose deployment status as an in-memory lifecycle snapshot

The remaining step is wiring that normalized graph into the real source / worker / graph lifecycle so that `ApplyDeploymentRequest.ExecutionRequest` becomes an executing runtime topology rather than only an accepted control-plane intent.
