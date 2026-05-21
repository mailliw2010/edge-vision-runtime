#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  generate-dual-camera-worker-configs.sh \
    --rtsp-uri 'rtsp://user:pass@camera/path' \
    [--mode direct|zlm] \
    [--output-dir configs] \
    [--zlm-base-uri rtsp://127.0.0.1:8554/live] \
    [--backend synthetic|tensorrt]

Default behavior:
  - mode=direct
  - writes:
      runtime-worker.camera-0.direct.local.yaml
      runtime-worker.camera-1.direct.local.yaml

When mode=zlm:
  - source_uri points to:
      <zlm-base-uri>/camera-0
      <zlm-base-uri>/camera-1
  - upstream_endpoint keeps the original RTSP URI
EOF
}

require_value() {
  local name="$1"
  local value="$2"
  if [[ -z "$value" ]]; then
    echo "missing required option: $name" >&2
    usage
    exit 1
  fi
}

rtsp_uri=""
mode="direct"
output_dir="configs"
zlm_base_uri="rtsp://127.0.0.1:8554/live"
backend="synthetic"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rtsp-uri) rtsp_uri="${2:-}"; shift 2 ;;
    --mode) mode="${2:-}"; shift 2 ;;
    --output-dir) output_dir="${2:-}"; shift 2 ;;
    --zlm-base-uri) zlm_base_uri="${2:-}"; shift 2 ;;
    --backend) backend="${2:-}"; shift 2 ;;
    --help|-h) usage; exit 0 ;;
    *)
      echo "unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

require_value --rtsp-uri "$rtsp_uri"

if [[ "$mode" != "direct" && "$mode" != "zlm" ]]; then
  echo "unsupported mode: $mode" >&2
  exit 1
fi

mkdir -p "$output_dir"

write_config() {
  local camera_id="$1"
  local source_uri="$2"
  local upstream_kind="$3"
  local upstream_endpoint="$4"
  local suffix="$5"
  local file_path="${output_dir}/runtime-worker.${camera_id}.${suffix}.local.yaml"

  cat >"$file_path" <<EOF
source:
  session_id: ${camera_id}
  source_uri: ${source_uri}
  upstream_kind: ${upstream_kind}
  upstream_endpoint: ${upstream_endpoint}
  transport_protocol: rtsp
  buffer_transport: host-memory
  proto_version: v1
  decode_mode: gstreamer-nv12-host
  pixel_format: nv12
  decode_timeout_seconds: 60
  decode_log_path: /tmp/evr_runtime_worker_${camera_id}_${suffix}.log

worker:
  session_id: worker-${camera_id}
  supervisor_endpoint: unix:///tmp/evr-supervisor.sock
  source_session_id: ${camera_id}
  proto_version: v1
  inference_backend: ${backend}
  engine_path: tests/fixtures/readable-yolov8s.model
  algorithm_name: yolov8-person-detection
  algorithm_package_uri: algorithm/yolov8_person_detection
  algorithm_entry_point: evr::algorithm::yolov8_person_detection::YoloV8PersonDetector
  algorithm_runtime_config_uri: algorithm/yolov8_person_detection/configs/yolov8_person_detection.v1.example.yaml
  input_binding: frames
  result_encoding: json
  output_topic: events.detection.${camera_id}

observability:
  enabled: true
  socket_path: /tmp/edge-vision-observe/runtime.sock
  preview_dir: /tmp/edge-vision-observe/previews
EOF

  echo "wrote ${file_path}"
}

if [[ "$mode" == "direct" ]]; then
  write_config "camera-0" "$rtsp_uri" "direct-rtsp" "$rtsp_uri" "direct"
  write_config "camera-1" "$rtsp_uri" "direct-rtsp" "$rtsp_uri" "direct"
else
  zlm_base="${zlm_base_uri%/}"
  write_config "camera-0" "${zlm_base}/camera-0" "zlm-proxy" "$rtsp_uri" "zlm"
  write_config "camera-1" "${zlm_base}/camera-1" "zlm-proxy" "$rtsp_uri" "zlm"
fi
