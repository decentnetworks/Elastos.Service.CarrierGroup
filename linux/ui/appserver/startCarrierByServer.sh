#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
# Carrier SDK shared libs (libcarrier, libcarriersession, libcrystal). The private 2019 copy
# under linux/service/lib is gone; point CARRIER_LIB_DIR at the installed SDK prefix's lib
# (make install of Elastos.NET.Carrier.Native.SDK) or at its build tree.
CARRIER_LIB_DIR="${CARRIER_LIB_DIR:-/usr/local/lib}"
VENV_PYTHON="${PROJECT_ROOT}/linux/ui/venv_chatrobot/bin/python"

HOST="${1:-127.0.0.1}"
PORT="${2:-5000}"
DATA_PATH="${3:-${PROJECT_ROOT}/linux/ui/runtime_data}"

mkdir -p "${DATA_PATH}"
if [ ! -e "${CARRIER_LIB_DIR}/libcarrier.so" ]; then
    echo "startCarrierByServer: libcarrier.so not found in CARRIER_LIB_DIR=${CARRIER_LIB_DIR}" >&2
    exit 1
fi
export LD_LIBRARY_PATH="${CARRIER_LIB_DIR}:${LD_LIBRARY_PATH:-}"

if [ -x "${VENV_PYTHON}" ]; then
    exec "${VENV_PYTHON}" "${SCRIPT_DIR}/server.py" --ip "${HOST}" --port "${PORT}" --data_path "${DATA_PATH}"
fi

exec python3 "${SCRIPT_DIR}/server.py" --ip "${HOST}" --port "${PORT}" --data_path "${DATA_PATH}"
