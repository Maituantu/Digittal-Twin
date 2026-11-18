#!/usr/bin/env python3
"""Render a systemd unit for the receiving Pi; never install or start it."""
import argparse
import grp
from pathlib import Path
import pwd
import sys


def quote(value):
    if any(char in value for char in ('\n', '\r', '\0')):
        raise ValueError('Invalid control character in systemd path')
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%') + '"'


def render(root, user, config):
    account = pwd.getpwnam(user)
    if account.pw_uid == 0:
        raise ValueError('Choose a non-root service user')
    root = root.resolve(strict=True)
    config = config.resolve(strict=True)
    executable = root / '.venv/bin/dt-cam'
    if not executable.is_file():
        raise ValueError('Run bash scripts/setup.sh before rendering the unit')
    if str(root) != str(root).strip() or any(char in str(root) for char in ('\n', '\r', '\0')):
        raise ValueError('Project directory contains unsupported whitespace')
    group = grp.getgrgid(account.pw_gid).gr_name
    text = (root / 'deploy/systemd/digital-twin-cam.service').read_text()
    replacements = {
        'User=thang': f'User={account.pw_name}',
        'Group=thang': f'Group={group}',
        'WorkingDirectory=/home/thang/digital_twin_cam': f'WorkingDirectory={str(root).replace(chr(37), chr(37) * 2)}',
        'ExecStart=/home/thang/digital_twin_cam/.venv/bin/dt-cam --config /home/thang/digital_twin_cam/config/local.yaml publish':
            f'ExecStart={quote(str(executable))} --config {quote(str(config))} publish',
    }
    for original, replacement in replacements.items():
        if text.count(original) != 1:
            raise ValueError('Service template changed; review renderer before installing')
        text = text.replace(original, replacement)
    return text.replace('# TEMPLATE: edit User, Group and WorkingDirectory before installing.',
                        '# Generated for this Pi. Review before installing.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--user', required=True, help='Account that owns the project and key; never root')
    parser.add_argument('--config', default='config/local.yaml')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    try:
        candidate = Path(args.config).expanduser()
        config = candidate if candidate.is_absolute() else root / candidate
        sys.stdout.write(render(root, args.user, config))
    except (OSError, KeyError, ValueError) as exc:
        parser.exit(1, f'{type(exc).__name__}: {exc}\n')


if __name__ == '__main__':
    main()
