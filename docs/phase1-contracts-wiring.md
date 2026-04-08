# phase-1 contracts wiring for runtime

当前阶段，Runtime 与 contracts 的接线采用两步策略：

## 第一步（当前阶段）

- 先冻结 `edge-vision-contracts` 的 proto v1 骨架
- Runtime 继续维护自身最小可编译骨架
- 在 Runtime 内引入 **phase-1 bridge** 的概念，把 contracts 里的概念映射到：
  - `SupervisorApp`
  - `Phase1DeploymentSpec`
  - `SourceSession`
  - `WorkerSession`

这一阶段的目标是：

- control-plane 已能围绕 contracts 编译与组织请求
- runtime 已能围绕 phase-1 deployment wiring 稳定表达其本地关系
- 不急于把 C++ 端的 proto/gRPC 生成、链接、ABI 细节一次性全部推上来

## 第二步（下一阶段）

- 再把 Runtime 的 control surface 收敛到 contracts 的 runtime/v1 上
- 逐步引入：
  - ApplyDeployment
  - GetSupervisorStatus
  - GetDeploymentStatus
- 再视情况决定：
  - C++ 代码生成策略
  - 生成物是否入仓
  - gRPC / IPC 的承接方式

## 原因

Jetson / GStreamer / TensorRT 这条线本来就足够复杂。
如果在当前阶段把：

- contracts 调整
- C++ proto 生成
- gRPC 接线
- Runtime 本地 wiring

同时做深，返工风险会明显变高。

所以当前推荐：

> 先让 control-plane 吃 contracts，
> Runtime 先通过 phase-1 bridge 对齐 contracts 语义，
> 再进入真实接线。
