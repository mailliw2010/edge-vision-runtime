# scripts/

本目录保存 `edge-vision-runtime` 的本地辅助脚本。

## 脚本清单

### `check_jetson_hw_decode_env.sh`

检查 Jetson 硬解环境和最小 GStreamer 硬解探针依赖。

### `generate-dual-camera-worker-configs.sh`

根据一条 RTSP 地址，一次性生成 `camera-0` / `camera-1` 两份 worker 本地配置。

直连原始流：

```bash
./scripts/generate-dual-camera-worker-configs.sh \
  --rtsp-uri 'rtsp://user:pass@camera/path'
```

走 ZLM outlet：

```bash
./scripts/generate-dual-camera-worker-configs.sh \
  --mode zlm \
  --rtsp-uri 'rtsp://user:pass@camera/path' \
  --zlm-base-uri 'rtsp://127.0.0.1:8554/live'
```

默认输出到 `configs/`。
