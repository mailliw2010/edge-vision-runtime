# ZLMediaKit runtime integration

ZLMediaKit is the media gateway in front of runtime. Runtime should consume the stable ZLM
play URL, while deployment/configuration keeps the original camera URL as upstream metadata.

## Target flow

```text
camera RTSP
  -> ZLMediaKit addStreamProxy
  -> rtsp://127.0.0.1:8554/live/camera-0
  -> runtime source
  -> decode
  -> Detect
  -> JSON result
```

## Runtime config shape

```yaml
source:
  source_uri: rtsp://127.0.0.1:8554/live/camera-0
  upstream_kind: zlm-proxy
  upstream_endpoint: rtsp://camera-host/path
  transport_protocol: rtsp
```

Do not store camera credentials in committed config. Use deployment-time secret injection or an
operator-local command when creating the ZLM proxy.

## Create a ZLM pull proxy

ZLMediaKit exposes `/index/api/addStreamProxy` for dynamic pull proxies. The API requires the
server `secret`, virtual host, app, stream id, and upstream `url`.

For a local container, keep RTSP on `8554` and map ZLM's HTTP API to an unused high port such as
`18080`:

```bash
docker run -d --name evr-zlm \
  -p 1935:1935 \
  -p 18080:80 \
  -p 8443:443 \
  -p 8554:554 \
  -p 10000:10000 \
  -p 10000:10000/udp \
  -p 8000:8000/udp \
  -p 9000:9000/udp \
  zlmediakit/zlmediakit:master
```

```bash
ZLM_API='http://127.0.0.1:18080'
ZLM_SECRET='<zlm-secret>'
CAMERA_RTSP='<camera-rtsp-url>'

curl -sS -X POST "${ZLM_API}/index/api/addStreamProxy" \
  -d "secret=${ZLM_SECRET}" \
  -d 'vhost=__defaultVhost__' \
  -d 'app=live' \
  -d 'stream=camera-0' \
  -d "url=${CAMERA_RTSP}" \
  -d 'rtp_type=0' \
  -d 'retry_count=-1' \
  -d 'timeout_sec=10' \
  -d 'enable_rtsp=1' \
  -d 'enable_rtmp=0' \
  -d 'enable_hls=0' \
  -d 'enable_mp4=0'
```

The expected runtime-facing URL is:

```text
rtsp://127.0.0.1:8554/live/camera-0
```

## Validate the ZLM outlet

First validate ZLM itself:

```bash
ffprobe -hide_banner -rtsp_transport tcp rtsp://127.0.0.1:8554/live/camera-0
```

Then run the runtime optional smoke:

```bash
cd projects/edge-vision-runtime/build
EVR_TEST_ZLM_RTSP_URI='rtsp://127.0.0.1:8554/live/camera-0' \
  ctest --output-on-failure -R runtime_zlm_video_detect_result_smoke_test
```

If this passes, the media path is proven through:

```text
ZLM outlet RTSP -> ffmpeg decode -> RGBA frames -> detector -> JSON result
```

## Current staging

1. `runtime_file_video_detect_result_smoke_test` validates file video decode to detection.
2. `runtime_rtsp_video_detect_result_smoke_test` validates direct camera RTSP when
   `EVR_TEST_RTSP_URI` is set.
3. `runtime_zlm_video_detect_result_smoke_test` validates the ZLM outlet when
   `EVR_TEST_ZLM_RTSP_URI` is set.

The file / direct RTSP / ZLM smoke tests now route decode through `SourceSession`. The current
implementation still uses bounded ffmpeg CLI capture as the portable phase-1 bridge; the next
runtime-only step is replacing that bridge with a long-lived SourceSession decode loop backed by
GStreamer / NVDEC / DeepStream on Jetson.

## Current ARM camera compatibility note

The ZLM outlet path has been validated with a synthetic stream:

```text
ffmpeg testsrc -> rtmp://127.0.0.1:1935/live/evr-smoke
  -> ZLMediaKit
  -> rtsp://127.0.0.1:8554/live/evr-smoke
  -> runtime_zlm_video_detect_result_smoke_test
```

The known camera URL below is viewable from a PC VLC on the same network, but the current ARM
runtime host cannot pull it with ffmpeg, GStreamer, or ZLM `addStreamProxy`:

```text
rtsp://<user>:<password>@192.168.11.198:554/h264/ch1/main/av_stream
```

Use the alternate RTSP service on port 5504 for the ARM runtime smoke path:

```text
rtsp://<user>:<password>@192.168.11.198:5504/h264/ch1/main/av_stream
```

Observed behavior on the ARM host:

- Raw RTSP `OPTIONS` receives `200 OK`
- ffmpeg/ffprobe fail at `DESCRIBE` with `400 Bad Request`
- ZLM `addStreamProxy` fails with `DESCRIBE:400 Bad Request`
- GStreamer `rtspsrc` times out during server option retrieval for several compatibility modes
- Pre-sending Basic authentication and changing User-Agent did not make `DESCRIBE` succeed

This points to a camera/client compatibility issue before runtime decoding begins. The next useful
check is to install/use a live555-based client on the ARM host, such as VLC or `openRTSP` from
`livemedia-utils`, and compare its request behavior with the PC VLC that can play the stream.

## Validated camera path

The runtime and ZLM path has also been validated with a working camera stream on the same network,
without committing credentials:

```text
camera 192.168.11.198 RTSP
  -> ZLMediaKit addStreamProxy stream=camera-198
  -> rtsp://127.0.0.1:8554/live/camera-198
  -> runtime_zlm_video_detect_result_smoke_test
```

Validation commands:

```bash
EVR_TEST_RTSP_URI='rtsp://<user>:<password>@192.168.11.198:5504/<path>' \
  ctest --output-on-failure -R runtime_rtsp_video_detect_result_smoke_test

EVR_TEST_ZLM_RTSP_URI='rtsp://127.0.0.1:8554/live/camera-198' \
  ctest --output-on-failure -R runtime_zlm_video_detect_result_smoke_test
```

Both tests passed on the ARM runtime host. ZLM reported H264 video at 1920x1080 and AAC audio for
the proxied stream.

The `runtime-worker` CLI can also consume the ZLM outlet directly when source-frame decoding is
explicitly enabled:

```bash
./build/bin/runtime-worker \
  --config configs/runtime-worker.v1.example.yaml \
  --source-uri rtsp://127.0.0.1:8554/live/camera-198 \
  --decode-source-frames \
  --frame-width 640 \
  --frame-height 360 \
  --frame-count 3 \
  --engine-path "$(pwd)/models/yolov8s.onnx" \
  --algorithm-package-uri "file://$(pwd)/algorithm/yolov8_person_detection" \
  --algorithm-entry-point evr::algorithm::yolov8_person_detection::YoloV8PersonDetector \
  --algorithm-runtime-config-uri "file://$(pwd)/algorithm/yolov8_person_detection/configs/yolov8_person_detection.v1.example.yaml"
```

The production inference path uses TensorRT. For repeatable CTest smoke runs, tests override the
algorithm backend to `synthetic`; this keeps video decode and result wiring testable without CUDA
device access. Running the command above with the default `tensorrt` backend requires TensorRT,
CUDA device access, and a valid `models/yolov8s.onnx` or serialized `.engine/.plan` file.

Without `--decode-source-frames`, `runtime-worker` keeps using a synthetic frame so default CTest
does not require ZLM or a camera.
