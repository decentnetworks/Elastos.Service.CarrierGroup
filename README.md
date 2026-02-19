# Elastos.Service.CarrierGroup

This project provides a Carrier group manager and a small HTTP API server for creating and listing group instances.

## Overview

- `linux/manager`: native manager library (`libcarrierManager.so`) exposed to Python via `ctypes`.
- `linux/service`: native per-group service executable (`carrierService`).
- `linux/ui/appserver`: Flask API bridge to manager/service.
- `linux/ui/webserver/chatrobot_server`: Vue frontend demo.

## Changes Added In This Branch

### Runtime and portability fixes

- Removed hardcoded `/home/lcf/...` paths and `python3.6` assumptions.
- `startCarrierByServer.sh` now computes paths relative to repo root and sets `LD_LIBRARY_PATH` automatically.
- `server_wsgi.py` now reads `chatrobot_config.ini` by absolute script-relative path and parses `socket_port` as integer.
- `server.py` now supports configurable manager host/port and proper CLI port handling.
- `chatrobot_restful_api.py` now loads `libcarrierManager.so` by absolute path and uses explicit `ctypes` argtypes.

### Carrier display-name fix (friendinfo/name issue)

- `CarrierRobot` now calls `ela_set_self_info(...)` at startup to publish a non-empty group display name.
- Group display name is resolved from group nickname (`group_info_table`) with safe fallback (`CarrierGroup-<service_id>`).
- Incoming friend display names now use fallback `CarrierUser-<idprefix>` when Carrier returns empty/blank name.

## Prerequisites

Ubuntu/Debian packages:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake \
  python3 python3-venv python3-pip \
  libsqlite3-dev libssl-dev libffi-dev
```

Python dependencies:

```bash
cd linux/ui
python3 -m venv venv_chatrobot
source venv_chatrobot/bin/activate
pip install wheel
pip install flask flask_cors uwsgi
deactivate
```

## Build Native Components

From repo root:

```bash
cmake -S linux/manager -B build/manager
cmake --build build/manager -j

cmake -S linux/service -B build/service
cmake --build build/service -j
```

Expected outputs:

- `build/manager/libcarrierManager.so`
- `build/service/carrierService`

## Install Build Outputs

Copy freshly built artifacts to appserver runtime location:

```bash
cp build/manager/libcarrierManager.so linux/ui/appserver/libcarrierManager.so
cp build/service/carrierService linux/ui/appserver/carrierService
```

## Configure

Edit:

- `linux/ui/appserver/chatrobot_config.ini`

Example:

```ini
[chatrobot]
data_dir = /home/<user>/devs/Elastos.Service.CarrierGroup/linux/ui/runtime_data
socket_ip = 127.0.0.1
socket_port = 2222
```

Create data directory if missing:

```bash
mkdir -p linux/ui/runtime_data
```

## Run (No Nginx Required)

```bash
cd linux/ui/appserver
./startCarrierByServer.sh 127.0.0.1 5000 /home/<user>/devs/Elastos.Service.CarrierGroup/linux/ui/runtime_data
```

Test API in another terminal:

```bash
curl -sS http://127.0.0.1:5000/groups
curl -sS http://127.0.0.1:5000/create
```

## API

- `GET /groups`: list groups (id/address/members/nickname)
- `GET /create`: create one new group

## Nickname Notes

- Group nicknames shown in `/groups` are stored in manager DB: `chatrobotmanager.db`.
- Per-service Carrier display names are now set from service group nickname during startup.
- If friend display name is empty from network data, fallback name is used to avoid blank entries.

## Troubleshooting

### `sqlite3.h: No such file or directory`

Install dev package:

```bash
sudo apt install -y libsqlite3-dev
```

### `curl ... connection refused`

- Ensure appserver process is still running.
- Start from `linux/ui/appserver` and keep that terminal open.
- Check port conflict on `5000` and manager socket port (`socket_port` in config).

