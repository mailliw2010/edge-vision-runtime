# runtime phase-1 bridge contract notes

这个目录现在只做最小桥接，不直接把真实 gRPC/IPC 链路一次性接进来。

## 已经稳定的约束

- `SupervisorSession` 表达 control-plane 侧的运行拓扑入口
- `Phase1DeploymentSpec` 表达 supervisor/source/worker 的最小 wiring
- `WorkerSession` 需要稳定接收 source / supervisor 相关配置
- `SourceSession` 需要稳定接收 source URI 与 proto/version 约束

## 建议的下一步

1. 把 `SupervisorApp` 的 deployment 语义对齐 contracts/runtime/v1 的 request/response 形状
2. 把 `RuntimeConfigLoader` 的配置字段继续收敛到 phase-1 需要的最小字段
3. 再进入真正的 contracts 消息层接线

## 不建议现在做的事

- 直接把 C++ 端完整 proto 生成链全部铺开
- 一次性补齐所有控制面 RPC
- 现在就加重的 gRPC 依赖图
