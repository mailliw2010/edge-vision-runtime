# Runtime Observability Design

目标：让 `edge-vision-runtime` 在不承担完整 dashboard / HTTP server 职责的前提下，稳定产出多路观测数据，供 `edge-vision-ops` 侧的 `observe-agent` 聚合。

## 1. 当前结论

`runtime` 不直接作为外部观测服务。

职责收敛为两类：

1. 主链内采样
   - decode / preprocess / infer / postprocess / e2e
   - queue depth / drop / reconnect / timeout
2. 本机导出
   - preview 原图
   - frontend OSD sidecar
   - runtime event / stream snapshot 消息流

对外接口由 `edge-vision-ops/observe-agent` 统一提供：

- 前端：WebSocket / HTTP
- Prometheus：`/metrics`

## 2. 通信架构

```text
runtime (C++)
  ├─ source / worker / detector
  ├─ preview files
  └─ UDS + NDJSON event stream
          |
          v
observe-agent (Go, edge-vision-ops)
  ├─ runtime stream ingest
  ├─ tegrastats sampler
  ├─ aggregation / retention
  ├─ WebSocket / HTTP for dashboard
  └─ /metrics for Prometheus
```

原因：

- runtime 的主战场是解码、推理、时延控制，不是 HTTP 服务
- UDS 是本机 IPC，更适合 runtime 和 observe-agent 之间的高频结构化通信
- NDJSON 第一阶段最容易调试，字段还在快速演进时更稳
- 协议稳定后，再考虑 length-prefixed protobuf

## 3. 为什么选 UDS + NDJSON

相比文件轮询：

- 没有轮询延迟
- 不需要目录扫描和文件原子替换约束
- 更适合高频小消息

相比 runtime 直接开 HTTP / WebSocket：

- 避免把 HTTP 路由、连接管理、SSE/WS 复杂度塞进 C++ 主进程
- 更容易把 runtime 和 dashboard 解耦
- 更方便后续用 Go 聚合 tegrastats、Prometheus、前端订阅

相比 gRPC：

- 第一阶段依赖和复杂度更低
- 调试时可直接 `socat` / `nc` 看流
- 字段还不稳定时改动成本低

## 4. runtime 需要产出的三类消息

所有消息先统一带一个最小 envelope：

```json
{
  "schema_version": "v1",
  "producer": {
    "name": "edge-vision-runtime",
    "version": "0.1.0"
  },
  "type": "stream_snapshot"
}
```

这里要区分三件事：

- `schema_version`
  - 消息结构版本
- `producer.version`
  - 运行中二进制版本
- `type`
  - 消息语义

不要把这三件事混成一个字段。

### 4.1 `stream_snapshot`

低频聚合快照，面向 Overview 卡片。

字段建议：

- `stream_id`
- `source_session_id`
- `decode_mode`
- `pixel_format`
- `buffer_transport`
- `preview_path`
- `metadata_path`
- `input_fps`
- `decoded_fps`
- `infer_fps`
- `queue_depth`
- `drop_total`
- `frame_age_ms`
- `last_detection_count`
- `healthy`
- `timing`

### 4.2 `frontend_osd_frame`

面向前端 OSD 叠框，不烧框到图像本身。

字段建议：

- `source_session_id`
- `frame_id`
- `ts_ms`
- `decode_mode`
- `pixel_format`
- `buffer_transport`
- `preview.path`
- `preview.width`
- `preview.height`
- `timing`
- `detections[].box`

### 4.3 `runtime_event`

面向错误、降级、重连、超时等离散事件。

字段建议：

- `level`
- `stream_id`
- `event`
- `message`
- `ts_ms`

## 5. preview 和 sidecar 的边界

当前主方案：

- preview 图像可以落本地文件
- OSD 元数据通过 UDS 流实时送给 observe-agent
- 前端用 canvas / overlay 自己画框

这样做的原因：

- 不把 CPU 花在 runtime 侧 JPEG 画框上
- 主推理链不需要为了调试而改成“烧框视频”
- 前端可以灵活开关框、标签、时延、confidence

## 6. runtime 内部接入点

当前最自然的采样位置：

- `SourceSession`
  - decode 成功/失败
  - frame count
  - decode timeout
- `FrameBuffer`
  - 后续建议继续挂 `frame_seq` 和时间戳
- `worker_app`
  - preprocess / infer / postprocess / e2e
  - detection count
  - preview sidecar

建议后续为帧元数据补这些时间点：

- `capture_ts_ms`
- `decode_done_ts_ms`
- `enqueue_ts_ms`
- `dequeue_ts_ms`
- `preprocess_start_ts_ms`
- `preprocess_end_ts_ms`
- `infer_start_ts_ms`
- `infer_end_ts_ms`
- `postprocess_end_ts_ms`

## 7. 当前代码骨架

第一阶段已经补了一个最小 publisher：

- [observe_publisher.h](/home/mic-711/.openclaw/workspace/projects/edge-vision-runtime/include/evr/runtime/observability/observe_publisher.h)
- [observe_publisher.cpp](/home/mic-711/.openclaw/workspace/projects/edge-vision-runtime/src/observability/observe_publisher.cpp)

职责很克制：

- 维护一个 Unix Domain Socket 连接
- 把三类消息编码成 NDJSON
- 发送给 observe-agent

它现在还没有接入主流程调度、重试策略、发送线程，这部分留到真正接 runtime 事件时再做。

## 8. 背压策略

这部分要提前定，不然多路时观测面会反咬主链。

建议原则：

- runtime 主推理线程不直接阻塞写 socket
- 真正接入时，通过独立 sender thread 消费 ring buffer
- 高频 snapshot / preview metadata 允许覆盖旧值
- `runtime_event` 不允许静默丢失

第一阶段 smoke 只验证：

- UDS 能连通
- NDJSON 格式稳定
- 三类消息都能送达

## 9. schema 版本规则

第一阶段先只用大版本：

- `v1`
- 后续必要时再升 `v2`

### `v1` 接收规则

- 未知字段：忽略
- 未知 `type`：忽略并记日志
- 未知 `schema_version`：拒绝解析并告警
- 缺少必填字段：标记 malformed

### 不需要升大版本的变更

- 新增可选字段
- 新增新的消息类型
- 放宽某个字段的取值范围

### 必须升大版本的变更

- 改字段名
- 改字段类型
- 改字段语义
- 改嵌套结构
- 删除仍被消费方依赖的字段

一个典型例子：

- `decoded_fps -> decode_fps`
- `preview_path -> preview.path`

这类变化都应从 `v1` 升到 `v2`。

## 10. 后续演进顺序

### Phase A

- 固定 UDS + NDJSON 契约
- 把 publisher 真正挂到 runtime 采样点
- `edge-vision-ops` 起 observe-agent

### Phase B

- observe-agent 聚合 runtime stream + `tegrastats`
- 提供 WebSocket / HTTP / Prometheus `/metrics`
- 起本地 dashboard

### Phase C

- 字段稳定后，评估 protobuf
- 引入更明确的背压、重连和版本协商

## 11. 不做什么

当前不做：

- runtime 直接开复杂 HTTP server
- runtime 直接服务 dashboard 静态资源
- runtime 直接提供完整 Prometheus 抓取面
- runtime 内嵌 WebSocket server

这些都属于 observe-agent 的职责。
