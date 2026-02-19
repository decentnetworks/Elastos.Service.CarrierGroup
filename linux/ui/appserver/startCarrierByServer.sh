#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SERVICE_LIB_DIR="${PROJECT_ROOT}/linux/service/lib/x86_64"
VENV_PYTHON="${PROJECT_ROOT}/linux/ui/venv_chatrobot/bin/python"

HOST="${1:-127.0.0.1}"
PORT="${2:-5000}"
DATA_PATH="${3:-${PROJECT_ROOT}/linux/ui/runtime_data}"

mkdir -p "${DATA_PATH}"
export LD_LIBRARY_PATH="${SERVICE_LIB_DIR}:${LD_LIBRARY_PATH:-}"

if [ -x "${VENV_PYTHON}" ]; then
    exec "${VENV_PYTHON}" "${SCRIPT_DIR}/server.py" --ip "${HOST}" --port "${PORT}" --data_path "${DATA_PATH}"
fi

exec python3 "${SCRIPT_DIR}/server.py" --ip "${HOST}" --port "${PORT}" --data_path "${DATA_PATH}"
