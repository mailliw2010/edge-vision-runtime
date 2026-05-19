#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE_BIN="${ROOT_DIR}/build/runtime_gstreamer_hw_decode_probe"

echo "== Jetson hardware decode environment =="

echo "-- device nodes --"
DEVICE_LIST="$(find /dev -maxdepth 1 \( -name 'nv*' -o -name 'video*' \) -printf '%f\n' 2>/dev/null | sort || true)"
if [[ -z "${DEVICE_LIST}" ]]; then
  echo "(none visible in current session)"
else
  printf '%s\n' "${DEVICE_LIST}"
fi

echo "-- key libraries --"
ldconfig -p 2>/dev/null | rg -n 'libcuda|libnvbufsurface|libnvbufsurftransform|libgstnv|libnvv4l2' || true

run_gst_inspect() {
  local element="$1"
  local log_file="/tmp/${element}.gstinspect.log"
  gst-inspect-1.0 "${element}" >"${log_file}" 2>&1
  local rc=$?
  if [[ ${rc} -eq 0 ]]; then
    printf '%s: ok\n' "${element}"
    return 0
  fi

  if [[ ${rc} -eq 139 ]]; then
    printf '%s: crashed during plugin init\n' "${element}"
  else
    printf '%s: missing or failed to load (exit=%s)\n' "${element}" "${rc}"
  fi
  sed -n '1,20p' "${log_file}" || true
  return 0
}

echo "-- gstreamer elements --"
for element in nvv4l2decoder nvvidconv rtspsrc rtph264depay h264parse appsink; do
  run_gst_inspect "${element}"
done

if [[ ! -x "${PROBE_BIN}" ]]; then
  echo "probe binary not found: ${PROBE_BIN}"
  exit 1
fi

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <rtsp-uri>"
  exit 2
fi

echo "-- hardware decode probe --"
set +e
"${PROBE_BIN}" "$1"
RC=$?
set -e
if [[ ${RC} -eq 0 ]]; then
  exit 0
fi

if [[ ${RC} -eq 139 ]]; then
  echo "probe crashed during NVIDIA decode stack init; current session likely lacks required Jetson device access"
fi
exit "${RC}"
