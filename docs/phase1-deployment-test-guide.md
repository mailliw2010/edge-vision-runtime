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
`runtime_file_video_detect_result_smoke_test` 覆盖。

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
- `runtime_file_video_detect_result_smoke_test` 能完成临时 MP4 生成、解码、Detect、JSON 结果断言

## 6. 当前已知限制

- 还不是完整 gRPC / IPC 闭环
- 还没有真实 contracts runtime/v1 的传输层接线
- 已接入 TensorRT engine 加载和执行路径；ONNX 模型会先通过 `trtexec` 生成缓存 engine
- 还没有把 NVDEC / GStreamer / DeepStream 解码路径接入 `SourceSession`
- 还没有把 ZLMediaKit 作为入口网关纳入自动化 smoke
- 状态主要仍是占位 / 内存态
- runtime 侧的独立验证通过 runtime 仓自身的 CTest / smoke test 完成，control-plane 黑盒测试只校验其接入点是否可达

## 7. 最小验收标准

满足以下条件即可认为 phase-1 部署测试通过：

1. control-plane 能成功编译并启动
2. runtime 三个二进制都能编译并启动
3. supervisor 的 phase-1 deployment 能正确 normalize
4. deployment 的关键字段（node/runtime/revision）不漂移
5. 没有明显的编译错误或配置解析错误
