# phase-1 control-plane/runtime bridge

目标是让 control-plane 的 deployment 语义，和 runtime 的 phase-1 wiring 语义逐步对齐。

## control-plane 当前能表达的东西

- deployment 的创建请求
- node_id / runtime / revision_id / camera_id
- deployment 的 in-memory 状态流转
- runtime client 的 noop/placeholder 调用

## runtime 当前能表达的东西

- supervisor / source / worker 的 session 边界
- phase-1 deployment spec
- apply/status 占位
- 最小 wiring 校验

## bridge 的最小规则

control-plane 在创建 deployment 时，至少要稳定携带：

- `node_id`
- `runtime`
- `revision_id`

runtime 侧在接收 apply 时，至少要能映射到：

- `deployment_id`
- `node_id`
- `revision_id`
- `preferred backend` / runtime mode

## 暂时不追求

- 真正的 gRPC / IPC 链路
- 完整 proto 生成后的端到端消息编排
- 状态持久化

当前最重要的是：

> 先把语义边界固定下来，再补真实传输层。
