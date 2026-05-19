# Phase-1 部署测试指南

目标：先把第一阶段最小闭环跑起来，验证 control-plane 与 runtime 的基础 wiring。

## 1. 目标仓库

- `edge-vision-contracts`
- `edge-vision-control-plane`
- `edge-vision-runtime`

## 2. 先决条件

### Control-plane
- Go 1.22+
- `/snap/bin/go build ./...` 可用

### Runtime
- C++17 工具链
- CMake 3.16+
- Ninja / Make
- `cmake --build build` 可用
- 本地视频文件闭环 smoke 需要 `ffmpeg` 可用，用于生成临时 MP4 并解码为 RGBA 帧

### Media gateway
- ZLMediaKit 是后续真实摄像头接入时的视频入口网关 / 流媒体代理层
- 当前 runtime 单仓 smoke 不依赖 ZLM，优先验证本地视频文件和直接 RTSP 的执行链路
- 接入多路摄像头、多消费者复用、协议转换、录像或在线状态回调时，再把 ZLM 放回入口层

## 3. 当前 phase-1 口径

- control-plane 负责创建 deployment
- runtime 负责表达 supervisor/source/worker 的最小 wiring
- 当前阶段优先跑通“语义闭环”。默认 smoke 使用 `synthetic` 后端保证测试稳定；
  真实最小闭环的本机推理后端是 TensorRT
- 视频执行链路按 `file.mp4 -> decode -> Detect -> result`、`rtsp://camera -> decode -> result`、
  `camera -> ZLMediaKit -> runtime` 的顺序推进

## 4. 启动顺序

### 4.1 启动 control-plane

```bash
cd projects/edge-vision-control-plane
/snap/bin/go build ./...
/snap/bin/go run ./cmd/api-server
```

默认检查：
- `GET /healthz`
- `GET /readyz`
- `POST /v1/deployments`

黑盒冒烟脚本可选传入 runtime 接入点：

```bash
BASE_URL=http://127.0.0.1:8080 RUNTIME_BASE_URL=http://127.0.0.1:8081 ./tests/system_smoke.py
```

其中 `RUNTIME_BASE_URL` 只用于检查 runtime 接入点是否可达，不替代 runtime 仓自身的测试。

control-plane 黑盒测试会额外确认 deployment 响应中的 `camera_id` / `algorithm_id` 绑定是否正确。

### 4.2 启动 runtime supervisor

```bash
cd projects/edge-vision-runtime
cmake -S . -B build
cmake --build build
./build/bin/runtime-supervisor apply --config configs/runtime-phase1-deployment.v1.example.yaml
```

### 4.3 启动 runtime worker

```bash
cd projects/edge-vision-runtime
./build/bin/runtime-worker --config configs/runtime-worker.v1.example.yaml
```

当前 `runtime-worker` CLI 会先跑通 source/worker 生命周期、算法配置加载、模型可读性检查、
合成帧 Detect 和 detection 输出。真实视频文件闭环由 CTest 中的
`runtime_file_video_detect_result_smoke_test` 覆盖，解码入口统一走 `SourceSession`。

单路流 + 1 算法的本地可重复入口：

```bash
ctest --output-on-failure -R runtime_file_video_detect_result_smoke_test
```

多路流 + 1 算法的本地可重复入口：

```bash
ctest --output-on-failure -R runtime_multi_stream_one_algorithm_smoke_test
```

当前多路 smoke 使用两个临时视频文件模拟两路 source，共用一个 YOLOv8 detector，验证每路
`source_session_id` 都能进入检测结果。真实 RTSP / ZLM 多路压测应在这个语义闭环稳定后，
再把 source URI 换成实际多路流。

GStreamer backend 的本地可重复入口：

```bash
ctest --output-on-failure -R runtime_gstreamer_video_decode_smoke_test
ctest --output-on-failure -R runtime_gstreamer_multi_stream_one_algorithm_smoke_test
```

Jetson 最小硬解链探针：

```bash
./build/runtime_gstreamer_hw_decode_probe \
  'rtsp://user:password@camera/stream'
```

这一步只验证：

```text
RTSP -> nvv4l2decoder -> nvvidconv -> appsink
```

用途是把“Jetson 硬解环境是否成立”和“多路流 / 算法 / 事件逻辑”拆开排查。详细说明见
[`docs/jetson-gstreamer-hw-decode-probe.md`](docs/jetson-gstreamer-hw-decode-probe.md)。

关于这条链路里每个元素在处理什么、以及为什么多路场景下通常应优先保留 NV12 而不是尽早转
RGBA，以及当前 `appsink` 队列深度和丢帧策略怎么理解，见
[`docs/jetson-rtsp-gstreamer-pipeline-notes.md`](docs/jetson-rtsp-gstreamer-pipeline-notes.md)。

该测试只在构建机存在 `gstreamer-1.0` 和 `gstreamer-app-1.0` 开发库时登记。当前 runtime
把 GStreamer 路径拆成两类：

- `decode_mode: gstreamer-rgba-host`
  - 当前已实现
  - 调试 / smoke / 最小闭环路径
  - 末端是 `appsink + gst_buffer_map + RGBA host bytes`
- `decode_mode: gstreamer-nv12-nvmm-device`
  - 作为生产取向路径的显式模式
  - 当前会返回明确错误
  - 要等 device-side preprocess / TensorRT tensor handoff 接上后再启用

兼容性上，旧的 `decode_mode: gstreamer` 仍会按 `gstreamer-rgba-host` 处理。
在缺少 Jetson `/dev/nvmap`、`/dev/nvhost-*` 等设备节点的会话里，真实 RTSP 的多路 GStreamer
smoke 会直接跳过，而不是进入插件初始化后崩溃。

当前 smoke 使用 `decode_mode: gstreamer-rgba-host` 和内建 `gst-testsrc://` 源走
`SourceSession` 的 appsink 解码路径，再接同一个 YOLOv8 detector 验证结果。没有
GStreamer 开发库时，runtime 仍保留 ffmpeg bridge 可编译。对真实 `rtsp://` 地址，
`gstreamer-rgba-host` 现在优先走显式 Jetson 硬解链：

```text
rtspsrc -> rtph264depay -> h264parse -> nvv4l2decoder -> nvvidconv -> appsink
```

因此，真实 RTSP 的成功与否首先取决于当前会话能否访问 Jetson 的 NVIDIA 设备节点和插件运行时。

多路真实 RTSP 调试建议直接改配置文件：

```yaml
test:
  frame_width: 640
  frame_height: 360
  frame_count: 3
  inference_backend: synthetic

stream.camera-main:
  source_uri: rtsp://192.168.11.198:5504/main
  upstream_kind: direct-rtsp
  transport_protocol: rtsp
  decode_mode: gstreamer-rgba-host
  pixel_format: rgba
  decode_timeout_seconds: 10

stream.camera-sub:
  source_uri: rtsp://192.168.11.198:5504/sub
  upstream_kind: direct-rtsp
  transport_protocol: rtsp
  decode_mode: gstreamer-rgba-host
  pixel_format: rgba
  decode_timeout_seconds: 10
```

默认配置在 `configs/runtime-multi-stream-gstreamer-smoke.v1.example.yaml`，为了让 CTest
稳定，它使用两路 `gst-testsrc://`。把 `source_uri` 换成内网 RTSP 后，直接运行：

```bash
./runtime_gstreamer_multi_stream_one_algorithm_smoke_test \
  ../configs/runtime-multi-stream-gstreamer-smoke.v1.example.yaml
```

直接 RTSP 视频源用可选 CTest 覆盖，不把带认证信息的 URL 写入仓库：

```bash
EVR_TEST_RTSP_URI='rtsp://user:password@camera/stream' \
  ctest --output-on-failure -R runtime_rtsp_video_detect_result_smoke_test
```

没有设置 `EVR_TEST_RTSP_URI` 时，该测试会被跳过。

ZLMediaKit 出口流用同一个解码路径验证，传入 ZLM 对外暴露的 RTSP 地址：

```bash
EVR_TEST_ZLM_RTSP_URI='rtsp://127.0.0.1:8554/live/camera-0' \
  ctest --output-on-failure -R runtime_zlm_video_detect_result_smoke_test
```

ZLM 接入时推荐先在 ZLM 侧把真实摄像头代理为稳定本机流：

```text
camera/upstream RTSP -> ZLMediaKit -> rtsp://127.0.0.1:8554/live/camera-0
```

runtime 的 `source_uri` 指向 ZLM 出口，`upstream_endpoint` 保留真实摄像头地址用于编排和排障。

### 4.4 启动 runtime source

```bash
cd projects/edge-vision-runtime
./build/bin/runtime-source --config configs/runtime-source.v1.example.yaml
```

## 5. 验证点

### Control-plane
- 能返回 health 状态
- `POST /v1/deployments` 能接受 `node_id / runtime / revision_id`
- 返回的 deployment 记录状态正常流转

### Runtime
- `runtime-supervisor` 能读取 phase-1 配置
- deployment spec 能 normalize
- apply/status 命令能打印 wiring 结果
- `runtime-worker` / `runtime-source` 能各自完成最小启动
- `runtime_file_video_detect_result_smoke_test` 能通过 `SourceSession` 完成临时 MP4 生成、解码、Detect、JSON 结果断言
- `runtime_gstreamer_video_decode_smoke_test` 能通过 `SourceSession` 的 GStreamer appsink backend 完成解码、Detect、结果断言
- `runtime_gstreamer_multi_stream_one_algorithm_smoke_test` 能从配置文件读取多路 source，并通过 GStreamer backend 共用一个 detector 完成结果断言
- `runtime_multi_stream_one_algorithm_smoke_test` 能用多个 `SourceSession` 共用一个 detector 完成多路 source 结果断言

## 6. 当前已知限制

- 还不是完整 gRPC / IPC 闭环
- 还没有真实 contracts runtime/v1 的传输层接线
- 已接入 TensorRT engine 加载和执行路径；ONNX 模型会先通过 `trtexec` 生成缓存 engine
- `SourceSession` 当前已有 ffmpeg bridge 和 GStreamer appsink backend；GStreamer 仍是按需拉取有限帧的 smoke 路径，还不是常驻 NVDEC / DeepStream pipeline
- ZLMediaKit 出口已作为可选 smoke 入口，仍依赖外部 ZLM 实例和 `EVR_TEST_ZLM_RTSP_URI`
- 多路 smoke 目前验证的是多路 source + 单算法语义，不是常驻并发调度器；生产形态还需要多 source runner 和 source -> worker 帧通路
- 状态主要仍是占位 / 内存态
- runtime 侧的独立验证通过 runtime 仓自身的 CTest / smoke test 完成，control-plane 黑盒测试只校验其接入点是否可达

## 7. 最小验收标准

满足以下条件即可认为 phase-1 部署测试通过：

1. control-plane 能成功编译并启动
2. runtime 三个二进制都能编译并启动
3. supervisor 的 phase-1 deployment 能正确 normalize
4. deployment 的关键字段（node/runtime/revision）不漂移
5. 没有明显的编译错误或配置解析错误
