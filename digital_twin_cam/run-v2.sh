#!/usr/bin/env bash
set -euo pipefail
cd /home/thang/digital_twin_cam
exec .venv/bin/dt-cam --config config/local.yaml "$@"
