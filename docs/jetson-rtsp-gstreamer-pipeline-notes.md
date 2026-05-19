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

## 2. NV12 和 RGBA 的核心差别

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

## 3. 从内存和拷贝角度看为什么 NV12 更适合多路

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

## 4. device memory / host memory 的关键分界

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

## 5. 什么时候“推理消费时再转 RGB”是正确的

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

## 6. 对多路并发量的实际影响

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

## 7. 对当前 runtime 的建议

当前 runtime 的 `appsink + gst_buffer_map + RGBA byte vector` 方案适合：

- 打通链路
- 最小闭环
- 单路/少路验证
- 调试定位

如果目标转到“多路真实生产吞吐”，更合理的下一阶段是：

1. RTSP 解码后默认保持 NV12/NVMM
2. 引入 device-side preprocess
3. 直接生成 TensorRT 输入 tensor
4. 只在需要截图/落盘/调试时再额外导出 RGBA/JPEG

## 8. 一句话结论

是，`解码后维持 NV12，而不是立刻转 RGBA`，通常能提升多路并发能力。  
真正的收益来自：

- 更小的像素体积
- 更少的颜色转换
- 更少的 device/host 来回搬运

前提是你后面的预处理和推理消费也尽量留在 device memory 上完成。
