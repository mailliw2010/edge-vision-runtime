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

## ZLMediaKit 的架构边界

ZLMediaKit 在本架构中定位为**视频入口网关 / 流媒体代理层**，不承担算法推理、
deployment 真相源或 runtime 状态编排职责。

它主要解决以下问题：

- 统一接入 RTSP / 推流 / 后续 GB28181 等不同上游视频入口
- 将不稳定或认证复杂的摄像头上游，代理成本机稳定的 `source_uri`
- 为 runtime、预览、录像、调试工具等多个消费者复用同一路上游流
- 把视频入口问题与 `SourceSession -> decode -> Worker -> result` 的执行链路解耦
- 为后续按需拉流、在线状态、协议转换、录像和流事件回调预留边界

因此，ZLM 对当前 runtime 单仓 smoke **不是硬依赖**。最小闭环优先按以下顺序推进：

1. 本地视频文件：`file.mp4 -> decode -> RGBA frame -> Detect -> JSON result`
2. 直接视频源：`rtsp://camera -> decode -> Detect -> result`
3. ZLM 入口：`camera/upstream -> ZLMediaKit -> rtsp://127.0.0.1:8554/... -> runtime`

这样可以先验证 runtime 能消费真实解码帧，再把 ZLM 放回入口层。进入多路视频、
多消费者复用、协议转换或生产化摄像头接入时，ZLM 基本会成为必要的系统边界。

ZLM 代理创建、出口验证和 runtime smoke 入口见
[`docs/zlmediakit-runtime-integration.md`](docs/zlmediakit-runtime-integration.md)。

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

## 测试规约

统一引用 control-plane 的 `TESTING.md`，单元测试用本仓语言栈，系统测试 / 黑盒测试优先用 Python。

## 测试入口

runtime 仓当前的验证优先走 CTest / smoke test：

```bash
cmake -S . -B build
cmake --build build
cd build
ctest --output-on-failure
```

最重要的 smoke 是：
- `runtime_sessions_smoke_test`

它覆盖：
- supervisor / source / worker session 基本生命周期
- phase-1 deployment normalize
- graph / wiring 生成
- example config loader

当前状态：已在当前机器执行，5/5 CTest 通过。

其他 CTest 也会覆盖：
- `runtime_source_cli_smoke_test`
- `runtime_worker_cli_smoke_test`
- `runtime_supervisor_apply_cli_smoke_test`
- `runtime_supervisor_status_cli_smoke_test`

其中：
- control-plane 黑盒脚本只校验 runtime 接入点是否可达
- runtime 自身的 wiring、normalize、状态机验证以本仓 CTest 为准

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
- `SourceApp` / `SourceSession`：承接 source URI、受限 ffmpeg 解码入口，并为后续拉流、解码、帧分发常驻化预留边界
- `WorkerApp` / `WorkerSession`：承接 TensorRT 推理、模型生命周期、本机 IPC

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
- Jetson NVDEC / GStreamer / DeepStream 常驻解码链路，以及 TensorRT engine 构建/部署策略完善
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
