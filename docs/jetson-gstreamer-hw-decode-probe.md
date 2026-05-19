# Jetson GStreamer Hardware Decode Probe

目标：把 `RTSP -> nvv4l2decoder -> nvvidconv -> appsink` 这条最小硬解链路单独拿出来验证，
避免把多路流、算法推理、事件输出等问题混进同一轮排查。

## 适用场景

- Jetson / ARM runtime 主机
- 需要确认 NVIDIA 硬解插件和设备节点是否可用
- 多路 RTSP + 1 算法测试前，先确认单路最小硬解链能出第一帧

## 探针程序

构建后会生成：

```bash
./build/runtime_gstreamer_hw_decode_probe
```

也可以直接用仓库脚本先做环境自检，再顺手跑探针：

```bash
bash ./scripts/check_jetson_hw_decode_env.sh \
  'rtsp://user:password@192.168.11.194/live/stream'
```

用法：

```bash
cd /home/mic-711/.openclaw/workspace/projects/edge-vision-runtime
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build

./build/runtime_gstreamer_hw_decode_probe \
  'rtsp://user:password@192.168.11.194/live/stream'
```

## 当前探针链路

程序内部固定验证：

```text
rtspsrc protocols=tcp latency=200
  -> rtph264depay
  -> h264parse
  -> nvv4l2decoder
  -> nvvidconv
  -> video/x-raw,format=RGBA
  -> appsink
```

这条探针对应 runtime 里的：

- `decode_mode: gstreamer-rgba-host`

它验证的是“显式 Jetson 硬解链能不能出第一帧”，不是最终的 `NV12/NVMM device-resident`
生产路径。

探针成功标准：

- pipeline 能进入 `PLAYING`
- appsink 能在 10 秒内拉到第一帧

成功时会输出类似：

```text
hardware decode probe succeeded for rtsp://***:***@host/path; first frame bytes=...
```

## 常见失败信号

`nvv4l2decoder is not available on this host`
: 没装好 Jetson 侧 GStreamer/NVIDIA 插件，或者当前环境不可见。

`nvvidconv is not available on this host`
: NVIDIA 颜色转换插件不可见。

`NvRmMemInitNvmap failed`
: 常见于容器/沙箱拿不到 Jetson 设备节点，或者当前运行环境不支持 NVIDIA 内存管理。

`pipeline did not reach PLAYING`
: 更偏向插件、RTSP 协商、设备节点、驱动或 caps 问题。

`hardware decode probe timed out before first frame`
: pipeline 启起来了，但没真正出第一帧。优先看 RTSP 流本身、解码器协商和 bus error。

## 排查顺序

1. 先跑这个探针，只测单路 RTSP。
2. 探针通过后，再跑 `runtime_gstreamer_multi_stream_one_algorithm_smoke_test`。
3. 多路 + 算法通过后，再考虑常驻 pipeline、NVDEC/DeepStream、性能压测。

## 结论边界

这个探针只回答一个问题：

`这台 Jetson 主机上的最小 GStreamer 硬解链，能不能从 RTSP 拉到第一帧。`

它不验证：

- 多路并发稳定性
- TensorRT 推理性能
- SourceSession 常驻调度
- ZLMediaKit 代理行为

更详细的链路解释和 NV12 / RGBA 的性能分析见
[`docs/jetson-rtsp-gstreamer-pipeline-notes.md`](docs/jetson-rtsp-gstreamer-pipeline-notes.md)。
