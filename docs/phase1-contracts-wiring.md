# phase-1 contracts wiring for runtime

当前阶段，Runtime 与 contracts 的接线策略已经收敛为：control-plane 用 Go 实现，runtime 的 gRPC control surface 由 C++ runtime 进程直接实现。

## 当前阶段

- 先冻结 `edge-vision-contracts` 的 proto v1 骨架
- Runtime 继续维护自身最小可编译骨架
- 在 Runtime 内引入 **phase-1 bridge** 的概念，把 contracts 里的概念直接映射到 C++：
  - `SupervisorApp`
  - `Phase1DeploymentSpec`
  - `SourceSession`
  - `WorkerSession`
- C++ runtime 直接实现 `runtime.v1.SupervisorService`：
  - `Handshake`
  - `ApplyDeployment`
  - `StopDeployment`
  - `GetSupervisorStatus`
  - `GetDeploymentStatus`

这一阶段的目标是：

- control-plane 已能围绕 contracts 编译与组织请求
- runtime 已能围绕 phase-1 deployment wiring 稳定表达其本地关系
- runtime 通过可选 CMake 开关 `EVR_ENABLE_GRPC=ON` 生成并链接 C++ proto/gRPC stub

## 不采用 Go runtime shim

runtime 内不引入 Go supervisor shim。边界固定为：

```text
control-plane (Go)
  -> gRPC / protobuf
runtime-supervisor (C++)
  -> C++ source / worker / graph
```

这样做的原因：

- 少一层 Go supervisor -> C++ worker 的内部协议。
- `ApplyDeploymentRequest.ExecutionRequest` 可以直接进入 C++ runtime 的部署规范和 graph wiring。
- GStreamer / TensorRT / 帧级数据面不跨语言边界。
- C++ runtime 进程天然拥有 source / worker / graph 的生命周期，直接实现 gRPC server 更符合所有权边界。

## 原因

Jetson / GStreamer / TensorRT 这条线本来就足够复杂。
如果把 Go shim 放在 runtime 侧，会额外引入：

- Go -> C++ 的内部启动/停止/状态协议
- control-plane proto 到内部协议的二次翻译
- 两套进程生命周期和错误语义

这些复杂度并不会减少 runtime 的 C++ 数据面复杂度。因此当前推荐 C++ runtime 直接承接 gRPC control surface。
