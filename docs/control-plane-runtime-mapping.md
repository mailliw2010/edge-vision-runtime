# control-plane to runtime execution mapping

This document freezes the current execution-layer contract between control-plane and the C++ runtime supervisor.

## Input

The runtime receives `runtime.v1.ApplyDeploymentRequest` with:

- `spec.deployment_id`
- `spec.node_id`
- `spec.artifact`
- `spec.config_revision`
- `spec.input_binding`
- `spec.execution_mode`
- `spec.execution_backend`
- `spec.execution`

`spec.execution` carries the declarative wiring:

- one or more `RuntimeSourceBinding` entries
- exactly one `RuntimeAlgorithmBinding`
- optional `RuntimeOutputBinding` entries
- execution policy and runtime-specific options

## Normalization rules

The runtime supervisor currently normalizes the request with the following rules:

- `Handshake` must happen before `ApplyDeployment`
- `spec.node_id` must match the local runtime node
- `execution_request_id` must match `deployment_id` when present
- at least one source binding is required
- exactly one algorithm binding is required
- every algorithm input binding must reference an existing source binding
- output bindings, when present, must have binding id and sink ref

## Execution graph

The accepted request is converted into an in-memory execution graph:

- one supervisor node
- one node per source binding
- one inference node for the algorithm binding
- zero or more output sink nodes
- edges from sources to the algorithm node
- a control edge from supervisor to the algorithm node
- edges from the algorithm node to output sinks

The graph is intentionally descriptive. It captures the control-plane intent and runtime topology, but it does not yet spawn the real long-lived source / worker processes.

## Lifecycle

The current in-memory lifecycle is:

- `Handshake` -> session established
- `ApplyDeployment` -> request accepted and normalized, state becomes `APPLYING`
- `GetDeploymentStatus` -> returns the latest accepted snapshot
- `StopDeployment` -> marks the deployment as stopped

This is enough to develop control-plane orchestration logic, status queries, and tests against a stable runtime boundary.

## Known gap

The missing final step is connecting the normalized graph to the real runtime lifecycle:

- `SourceSession` start/stop
- `WorkerSession` start/stop
- supervisor-driven graph activation
- status/event persistence beyond process memory

That work belongs to the next iteration of the runtime execution engine.
