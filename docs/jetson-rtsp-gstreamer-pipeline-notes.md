# Jetson RTSP GStreamer Pipeline Notes

目标：解释 runtime 当前采用的 Jetson RTSP 硬解链：

```text
rtspsrc -> rtph264depay -> h264parse -> nvv4l2decoder -> nvvidconv -> appsink
```

以及为什么在多路场景下，保留 NV12 往往比尽早转 RGBA 更有利。

## 1. 每一段在处理什么

### `rtspsrc`

职责：

- 和摄像头建立 RTSP 会话
- 发送 `OPTIONS / DESCRIBE / SETUP / PLAY`
- 接收 RTP 媒体包

输入：

- `rtsp://...`

输出：

- RTP 包流

要点：

- `protocols=tcp` 强制 RTP over RTSP/TCP，优先稳定性而不是最低延迟
- `latency=200` 给网络抖动留缓冲

### `rtph264depay`

职责：

- 从 RTP 包里剥出 H.264 负载

输入：

- RTP/H.264 网络包

输出：

- H.264 码流

### `h264parse`

职责：

- 对 H.264 码流做规整
- 处理 SPS/PPS、NAL 边界、时间戳和对齐方式

输入：

- H.264 码流片段

输出：

- 适合送进解码器的 H.264 elementary stream

### `nvv4l2decoder`

职责：

- Jetson 硬件 H.264 解码

输入：

- 压缩 H.264 码流

输出：

- 解码后的视频帧
- 通常是 NV12 / YUV420 一类格式
- 通常驻留在 Jetson 的 NVMM surface 里

要点：

- 这是省 CPU 的关键
- 多路流并发能力主要受这里和后面的内存带宽影响

### `nvvidconv`

职责：

- 在 Jetson 上做颜色转换、尺寸缩放、内存类型调整

输入：

- 解码器输出帧，通常是 NV12 in NVMM

输出：

- 下游要求的格式
- 当前最小探针用的是 `video/x-raw,format=RGBA`

要点：

- 如果你在这里就转 RGBA，等于很早把像素体积放大
- 如果你在这里保留 NV12，可以把颜色转换延后到真正需要 RGB 的地方

### `appsink`

职责：

- 把 GStreamer pipeline 的帧交给应用程序

输入：

- GStreamer buffer

输出：

- 用户态代码可拉取的 `GstSample`

要点：

- `appsink` 不是天然等于 host memory
- 但如果下游 caps 已经是 `video/x-raw,format=RGBA`，而你的应用只是普通 `gst_buffer_map`，
  实际上通常会走到 CPU 可访问的一份数据
- 真正要保持 device-resident 路径，通常需要 NVMM / DMABUF / EGLImage / CUDA interop 这类机制

## 2.5 `appsink` 队列在 host，还是在 device

这个问题需要拆成两层：

1. 队列控制结构在哪里
2. 队列里的帧 payload 在哪里

### 队列控制结构

`appsink` 是 GStreamer 的用户态元素，它的队列管理、限流参数、拉帧 API 都在 host 侧。

所以从控制逻辑看：

- `appsink max-buffers`
- `appsink drop`
- `queue max-size-buffers`
- `queue leaky`

这些都是 host 侧在控制。

### 帧 payload

但这不等于“队列里的图像数据一定在 host memory”。

更准确的说法是：

- `appsink` 队列里排的是 `GstSample / GstBuffer`
- `GstBuffer` 可以引用 host RAW buffer
- 也可以引用 NVMM / device-backed buffer

所以真正影响性能的不是“有没有 appsink”，而是：

- 你前面的 caps 和 allocator 怎么协商
- 你下游如何消费 buffer

### 当前 runtime 属于哪一种

当前 runtime 的 GStreamer 路径属于：

```text
RGBA host-consumed path
```

因为它做了两件关键事情：

1. pipeline 末端协商成 `video/x-raw,format=RGBA`
2. C++ 代码对 `GstBuffer` 做 `gst_buffer_map(..., GST_MAP_READ)`，再拷进 `std::vector<uint8_t>`

这意味着当前实现虽然前面用了 Jetson 硬解，但最后交给推理前的帧已经是 host 字节数组。

### 如果改成 NV12/NVMM device path

更合理的生产取向路径应该是：

```text
RTSP
-> nvv4l2decoder
-> video/x-raw(memory:NVMM),format=NV12
-> queue
-> device-side preprocess
-> TensorRT
```

这时：

- `queue` 元素的控制逻辑仍在 host
- 但队列里排队的 `GstBuffer` payload 可以继续引用 NVMM surface
- 只要不做 `gst_buffer_map + std::vector` 这类 host 拷贝，帧数据就不必掉回 host

所以准确结论是：

- 队列管理不需要搬到 GPU 上
- 真正需要保持在 device side 的，是帧 payload
- 多路性能优化关注的是“payload 是否留在 NVMM/device”，不是“queue 元素是不是跑在 GPU”

## 3. NV12 和 RGBA 的核心差别

### 像素体积

`NV12`

- 8-bit Y plane
- 8-bit interleaved UV plane
- 总体约 `1.5 bytes/pixel`

`RGBA`

- 4 channels x 8-bit
- 总体 `4 bytes/pixel`

对 1920x1080 一帧：

- NV12 约 `1920 * 1080 * 1.5 = 3.11 MB`
- RGBA 约 `1920 * 1080 * 4 = 8.29 MB`

结论：

- RGBA 一帧体积约是 NV12 的 `2.67x`
- 多路场景下，这个差异会直接放大显存/内存带宽压力、队列占用和 copy 成本

## 4. 从内存和拷贝角度看为什么 NV12 更适合多路

### 路径 A：早转 RGBA

```text
RTSP
-> depay/parse
-> nvv4l2decoder (NV12 in NVMM)
-> nvvidconv (RGBA)
-> appsink
-> CPU / host memory
-> 预处理
-> 推理
```

优点：

- 实现简单
- 应用拿到的是常见 RGBA 像素
- 适合调试、截图、快速 smoke

缺点：

- 颜色转换发生得太早
- 每帧数据体积放大
- 一旦 appsink 落到 host memory，就引入 device -> host copy
- 如果后面推理又要回 GPU，还会再来一次 host -> device copy

这条路径在多路场景下的问题不是“能不能跑”，而是：

- 内存带宽浪费
- CPU cache 压力变大
- 拷贝次数增加
- 每路缓冲区占用更大

### 路径 B：解码后保持 NV12

```text
RTSP
-> depay/parse
-> nvv4l2decoder (NV12 in NVMM)
-> appsink / custom sink / CUDA interop
-> 在真正消费前再做 resize / color convert / normalize
-> 推理
```

优点：

- 在大部分路径里都保留更小的像素体积
- 减少早期颜色转换
- 对多路流更友好
- 更容易把 resize/color convert/normalize 合并成一次 GPU 预处理

缺点：

- 实现复杂很多
- 应用层不能再假设“拿到的是现成 RGBA”
- 你需要处理 NVMM surface、DMABUF、EGL/CUDA 映射、或者至少一个显式 device preprocess

## 5. device memory / host memory 的关键分界

Jetson 上最值得避免的是这两次搬迁：

1. `device/NVMM -> host`
2. `host -> device`（推理前再传回 GPU）

如果 pipeline 是：

```text
nvv4l2decoder -> nvvidconv -> RGBA -> appsink -> CPU preprocess -> TensorRT
```

很容易出现：

- 解码在 device
- 转成 RGBA 后给到 host
- CPU 做预处理
- 再把 tensor 拷回 GPU

这是最常见、也最伤多路吞吐的模式。

理想路径是：

```text
nvv4l2decoder (NV12 in NVMM)
-> device-side preprocess
-> TensorRT input tensor
```

也就是：

- 解码在 device
- resize / crop / color convert / normalize 也在 device
- 直接产出模型输入 tensor
- 不落 host

## 6. 什么时候“推理消费时再转 RGB”是正确的

可以，而且通常更好，但有前提。

### 正确前提

- 模型消费链本身就在 GPU 上
- 你能在 GPU 上完成：
  - NV12 -> RGB
  - resize
  - normalize
  - layout transform（如 HWC -> CHW）

这种情况下，把 NV12 保留到推理前再转，是更优方案。

### 不那么划算的情况

- 你最后还是要把帧拉到 CPU
- 你的预处理逻辑主要写在 CPU 上
- 应用没有 NVMM/DMABUF/CUDA interop

这时“延后转 RGB”并不会消灭 copy，只是把 copy 往后挪。  
它可能仍然值得，但收益会比“全 device 路径”小很多。

## 7. 多路解码时的缓存队列和丢帧策略

先说当前 runtime 的真实行为，而不是理想设计。

### 当前实现

当前 `SourceSession` 的 GStreamer 路径没有单独插入 `queue` 元素，也没有暴露
“NV12 队列深度”配置项。现在真正被显式配置的是 `appsink` 内部队列。

另外，runtime 里现在把 GStreamer 路径语义拆成了三类：

- `gstreamer-rgba-host`
  - 当前可用
  - 调试和最小闭环路径
  - 输出 `RGBA + host vector`
- `gstreamer-nv12-host`
  - 当前可用
  - 过渡桥接路径
  - 输出 `NV12 + host vector`
  - detector 在这一路上直接做 `NV12 host -> preprocess`
- `gstreamer-nv12-nvmm-device`
  - 作为未来生产路径的显式模式
  - 当前会直接返回明确错误
  - 只有补齐 device-side preprocess / tensor handoff 后才应启用

兼容性上，旧的 `gstreamer` / `gst` / `gstreamer-appsink` 仍按 `gstreamer-rgba-host` 处理。

当前真正可跑通的 `rgba-host` 路径里，被显式配置的是 `appsink` 队列：

- RTSP Jetson 硬解路径：
  `src/source/source_session.cpp` 里是
  `appsink name=evr_sink sync=false max-buffers=<frame_count> drop=false`
- `gst-testsrc` smoke 路径也是同样设置

这意味着：

1. 当前队列深度按“帧数”限制，不按“字节数”限制
2. 当前深度等于调用方请求的 `frame_count`
3. 多路 smoke 默认配置里 `frame_count: 3`
4. 所以当前每一路 source 的 `appsink` 队列上限就是 `3` 帧

要点：

- 这不是“decoder 内部缓存深度”的完整上限
- `rtspsrc`、jitter buffer、decoder 自身仍有各自的内部缓冲
- runtime 当前唯一直接控制的是 `appsink max-buffers`

### 当前并不是 NV12 队列

虽然架构讨论里我们强调“应尽量保留 NV12”，但当前 runtime 的 RTSP/GStreamer 执行链
仍然是：

```text
rtspsrc -> rtph264depay -> h264parse -> nvv4l2decoder -> nvvidconv -> video/x-raw,format=RGBA -> appsink
```

所以现在堆在 `appsink` 里的其实是 RGBA 帧，不是 NV12 帧。  
如果后续改成 `NV12 in NVMM -> device-side preprocess`，队列语义仍然一样，但单帧占用会小很多。

### 当前丢帧策略

当前是：

- `drop=false`
- `sync=false`

`drop=false` 的含义是：

- 当 `appsink` 队列满时，不主动丢旧帧
- 上游会受到回压，继续生产会变慢甚至阻塞
- 对有限帧 smoke 友好，因为它优先保证“不丢请求范围内的帧”

这对测试是合适的，对常驻多路实时流未必合适。  
多路实时场景里，如果消费速度跟不上，`drop=false` 往往意味着：

- 延迟不断累计
- 每路都在消费旧帧
- 最后“看起来没掉帧”，但结果已经明显滞后

### 如果目标是实时优先，怎么设

实时视频分析通常更关心“最新帧”，不关心“每一帧都不丢”。这时更常见的策略是：

```text
max-buffers=1
drop=true
```

含义：

- `appsink` 只保留 1 帧
- 消费端慢时，旧帧被丢掉，尽量保最新帧
- 好处是延迟不容易无限累积
- 代价是检测结果不再覆盖全部帧

如果要更明确地把“排队”与“落帧”分开控制，通常会在 `appsink` 前加显式 `queue`：

```text
... ! queue max-size-buffers=1 leaky=downstream ! appsink max-buffers=1 drop=true
```

这类组合的意图是：

- `queue max-size-buffers=1`：队列深度只保留 1 帧
- `leaky=downstream`：队列满时丢旧帧，尽量让最新帧往下游走
- `appsink max-buffers=1 drop=true`：应用侧也不积压

如果你的目标不是最低延迟，而是“允许一点小抖动但别太容易掉帧”，更稳妥的折中值通常是：

- `max-size-buffers=2~4`
- `max-buffers=2~4`
- `drop=true`

这样能吸收一点抖动，但不会把延迟拉太长。

### 多路场景下推荐怎么选

建议按目标分三档：

1. 测试/排障
   - `max-buffers = frame_count`
   - `drop=false`
   - 目的：尽量保留样本，方便确认链路正确性

2. 在线实时检测
   - `queue max-size-buffers=1 leaky=downstream`
   - `appsink max-buffers=1 drop=true`
   - 目的：最新帧优先，避免累计延迟

3. 轻度抖动容忍
   - `queue max-size-buffers=2~4 leaky=downstream`
   - `appsink max-buffers=2~4 drop=true`
   - 目的：在轻微网络抖动和轻微推理抖动之间取平衡

### 对当前 runtime 的直接结论

当前仓库里，如果你什么都不改：

- 每路 `appsink` 队列深度 = `frame_count`
- 多路 GStreamer smoke 默认 = `3` 帧/路
- 当前策略 = `drop=false`
- 当前行为 = 保样本优先，不是低延迟优先

如果你要把它转成生产取向，下一步应该把这些参数从硬编码变成 `SourceSessionConfig` 可配项，
至少暴露：

- `decode_queue_max_buffers`
- `decode_drop_policy`
- 可选的 `decode_queue_leaky_mode`

## 8. 对多路并发量的实际影响

如果其他条件相同，保留 NV12 通常会提升多路并发能力，原因主要有三类：

1. 更小的帧体积
   - 同样的缓冲深度，占用更少内存
   - 同样的带宽，可以承载更多路

2. 更少的早期转换
   - 不必每路一上来就做 NV12 -> RGBA
   - 把颜色转换推迟到真正需要的位置

3. 更少的 host/device 搬迁机会
   - 尤其当推理也在 GPU 上时，收益最大

但它不是“白送的并发”。真正的上限还受这些因素制约：

- `nvv4l2decoder` 的会话数上限
- NVDEC/NVENC/NVCSI 资源
- 内存带宽
- 后处理和事件编码是否落到 CPU
- TensorRT 预处理和推理的 batch/stream 策略

## 9. 对当前 runtime 的建议

当前 runtime 的 `gstreamer-rgba-host` 方案，也就是
`appsink + gst_buffer_map + RGBA byte vector`，适合：

- 打通链路
- 最小闭环
- 单路/少路验证
- 调试定位

如果目标转到“多路真实生产吞吐”，更合理的下一阶段是让
`gstreamer-nv12-nvmm-device` 这条路径真正落地：

1. RTSP 解码后默认保持 NV12/NVMM
2. 引入 device-side preprocess
3. 直接生成 TensorRT 输入 tensor
4. 只在需要截图/落盘/调试时再额外导出 RGBA/JPEG

最近一版已经把接口边界向前推了一步：

- `FrameBuffer` 现在带 `pixel_format` / `buffer_transport` 元数据
- detector 不再只能吃 `RGBA`
- 算法侧已经支持 `NV12 host bytes -> preprocess -> detect`

这意味着后续如果 source 侧补出 `NV12 host` 或 `NV12/NVMM` 路径，worker 和 detector
不用再假设“所有帧都得先转成 RGBA 才能进入算法”。

## 10. 一句话结论

是，`解码后维持 NV12，而不是立刻转 RGBA`，通常能提升多路并发能力。  
真正的收益来自：

- 更小的像素体积
- 更少的颜色转换
- 更少的 device/host 来回搬运

前提是你后面的预处理和推理消费也尽量留在 device memory 上完成。
