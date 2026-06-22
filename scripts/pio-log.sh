#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="${1:-vision-master-e213}"
ACTION="${2:-build}"  # build | upload | uploadfs | monitor | test
PORT="${3:-}"

PIO_BIN="${PIO_BIN:-$HOME/.platformio/penv/bin/pio}"
LOG_DIR="$(cd "$(dirname "$0")/.." && pwd)/logs"
mkdir -p "$LOG_DIR"
TS="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/${ACTION}-${ENV_NAME}-${TS}.log"

cmd=("$PIO_BIN")
case "$ACTION" in
  build)
    cmd+=(run -e "$ENV_NAME")
    ;;
  upload)
    cmd+=(run -e "$ENV_NAME" --target upload)
    if [[ -n "$PORT" ]]; then
      cmd+=(--upload-port "$PORT")
    fi
    ;;
  uploadfs)
    cmd+=(run -e "$ENV_NAME" --target uploadfs)
    ;;
  monitor)
    cmd+=(device monitor)
    if [[ -n "$PORT" ]]; then
      cmd+=(--port "$PORT")
    fi
    ;;
  test)
    cmd+=(test -e "$ENV_NAME")
    ;;
  *)
    echo "Unknown action: $ACTION"
    echo "Usage: $0 [env] [build|upload|uploadfs|monitor|test] [port]"
    exit 2
    ;;
esac

echo "Running: ${cmd[*]}"
echo "Log: $LOG_FILE"
"${cmd[@]}" 2>&1 | tee "$LOG_FILE"
