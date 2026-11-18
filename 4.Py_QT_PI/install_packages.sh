#!/usr/bin/env bash
set -euo pipefail
if ! command -v apt-get >/dev/null 2>&1; then
  echo 'Can Raspberry Pi OS / Debian co apt-get.' >&2
  exit 1
fi
if [ "$(id -u)" -eq 0 ]; then
  apt-get update
  apt-get install -y python3 python3-pyqt5 fonts-dejavu-core
else
  sudo apt-get update
  sudo apt-get install -y python3 python3-pyqt5 fonts-dejavu-core
fi
/usr/bin/python3 -c 'from PyQt5 import QtCore, QtGui, QtWidgets; print("PyQt5 OK:", QtCore.QT_VERSION_STR)'
echo 'Da cai xong. Chay: python3 run.py'
