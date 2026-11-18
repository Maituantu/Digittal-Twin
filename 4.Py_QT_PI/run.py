#!/usr/bin/env python3
"""Start the Pi dashboard; install OS Qt packages when missing."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def main():
    if not sys.platform.startswith('linux'):
        sys.exit('Ban nay danh cho Raspberry Pi OS. Dung 4.Py_QT de demo tren PC.')
    if os.geteuid() == 0:
        sys.exit('Chay UI bang user desktop: python3 run.py (khong sudo).')
    python = '/usr/bin/python3'
    probe = subprocess.run([python, '-c', 'from PyQt5 import QtCore, QtGui, QtWidgets'],
                           capture_output=True, text=True)
    if probe.returncode:
        print('Dang cai PyQt5 qua apt (can Internet, co the hoi mat khau sudo)...', flush=True)
        subprocess.run(['bash', str(ROOT / 'install_packages.sh')], check=True)
    if not (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
        sys.exit('Can mo terminal tren desktop Pi de hien thi HDMI. Phien SSH hien tai chua co display.')
    # Use system Python because apt installs Qt for this interpreter, not a PC venv.
    args = sys.argv[1:] or ['--connect', '--fullscreen']
    os.chdir(ROOT)
    os.execv(python, [python, str(ROOT / 'main.py'), *args])


if __name__ == '__main__':
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit(f'Khong the khoi dong: {exc}. Kiem tra Internet/sudo roi chay lai python3 run.py.')
