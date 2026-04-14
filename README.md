# edge-vision-runtime

C++ Runtime 仓，当前聚焦**真实最小业务闭环**的运行时承载。

## 当前口径

当前最小业务闭环按以下真实架构收敛：

- ZLMediaKit：流媒体入口 / 代理 / 多路流前置能力
- SourceSession：承接来自 ZLM 或直接流源的视频输入
- GStreamer / DeepStream：解码与媒体处理链
- Worker：承接本机推理调用
- 本机推理：**当前最小闭环优先 TensorRT**
- Supervisor：负责编排 Source / Worker / graph / deployment 状态

当前阶段，runtime 已经开始同时维护：

- YAML：用户侧/部署侧入口配置
- JSON graph：内部 DAG / pipeline 表达
- C++ 对象模型：真正执行时的运行时承载

## 当前职责

本仓在最小闭环里负责：

- 承接 deployment 到 graph 的映射
- 承接 source / worker / supervisor 的运行时语义
- 表达视频流、算法节点、输出主题的最小业务链
- 为后续接入 ZLM、GStreamer/DeepStream、TensorRT 预留稳定边界

本仓当前不负责：

- 前端页面
- 控制面业务编排真相源
- 跨仓 REST / gRPC 契约定义

## 当前骨架

- `cmd/`：二进制入口
- `include/evr/runtime/`：公开接口与占位类型
- `src/`：最小实现
- `configs/`：v1 配置模板（现在可被极简 YAML-ish loader 读取，用于 phase-1 占位接线）
- `tests/`：smoke test

```text
.
├── cmd/
│   ├── runtime-source/
│   ├── runtime-supervisor/
│   └── runtime-worker/
├── configs/
├── include/evr/runtime/
│   ├── config/
│   ├── deployment/
│   ├── session/
│   ├── source/
│   ├── supervisor/
│   └── worker/
├── src/
│   ├── config/
│   ├── deployment/
│   ├── session/
│   ├── source/
│   ├── supervisor/
│   └── worker/
└── tests/
```

## 最小边界

这版只保留第一阶段真正需要的接口边界：

- `SupervisorApp` / `SupervisorSession`：后续承接调度、恢复、状态汇总；当前已挂上最小 deployment apply/status 占位
- `SourceApp` / `SourceSession`：后续承接拉流、解码、帧分发
- `WorkerApp` / `WorkerSession`：后续承接 TensorRT 推理、模型生命周期、本机 IPC

其中：

- `runtime-supervisor` 现在会在启动 supervisor session 后 apply 一个 phase-1 deployment 占位，并打印 status
- `runtime-source` 先独立保留 source 占位入口
- `runtime-worker` 暂时串起 source + worker，作为第一阶段最小闭环

## phase-1 占位接线

当前新增了两个很薄的层：

- `config::RuntimeConfigLoader`：读取 examples 这类简单 YAML-ish 文件，只支持 `section -> key: value` 结构
- `deployment::Phase1DeploymentSpec` / `DeploymentController`：显式表达 supervisor / source / worker 的引用关系，并提供 `apply/status` 占位接口

`Phase1DeploymentSpec::Normalize()` 当前会做几件最小但关键的事：

- 保证三者 `proto_version` 一致
- 保证 `worker.source_session_id == source.session_id`
- 保证 `worker.supervisor_endpoint == supervisor.control_endpoint`
- 在字段留空时做最小 auto-wire

这让 phase-1 的控制面关系先被固定下来，但仍然不引入真实 IPC、调度器或推理执行。

## 尚未接入

- proto v1 的真实消息定义与序列化
- Jetson NVDEC / TensorRT 具体适配
- supervisor <-> worker 的真实控制面链路
- source -> worker 的帧通路
- 完整 YAML 解析、schema 校验、CLI 传参接线（当前只有极简 YAML-ish loader，够读取 examples）

## 构建

```bash
cmake -S . -B build
cmake --build build
(cd build && ctest --output-on-failure)
```

## 安装（可选）

```bash
cmake -S . -B build -DEVR_ENABLE_INSTALL=ON
cmake --build build --target install
```
