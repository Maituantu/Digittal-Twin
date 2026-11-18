#!/usr/bin/env python3
"""Read-only Linux process/power sampling during an existing camera stream.

No cameras are opened, no processes restarted, no credentials/argv exported.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def processes():
    result = {}
    for directory in Path('/proc').glob('[0-9]*'):
        try:
            text = (directory / 'stat').read_text()
            closing = text.rfind(')')
            fields = text[closing + 2:].split()
            result[int(directory.name)] = {
                'name': text[text.index('(') + 1:closing],
                'ppid': int(fields[1]),
                'ticks': int(fields[11]) + int(fields[12]),
                'start': int(fields[19]),
                'rss_mib': int(fields[21]) * os.sysconf('SC_PAGE_SIZE') / 1048576,
            }
        except (OSError, ValueError, IndexError):
            continue
    return result


def family(snapshot, root):
    selected = {root} if root in snapshot else set()
    while True:
        children = {pid for pid, row in snapshot.items() if row['ppid'] in selected}
        expanded = selected | children
        if expanded == selected:
            return {pid: snapshot[pid] for pid in selected}
        selected = expanded


def discover():
    matches = []
    for directory in Path('/proc').glob('[0-9]*'):
        try:
            args = (directory / 'cmdline').read_bytes().split(b'\0')
            if any(Path(arg.decode()).name == 'dt-cam' for arg in args[:2] if arg):
                matches.append(int(directory.name))
        except (OSError, UnicodeError):
            continue
    if len(matches) != 1:
        raise ValueError(f'Expected one dt-cam publisher, found {len(matches)}; use --pid explicitly')
    return matches[0]


def throttle():
    try:
        result = subprocess.run(['vcgencmd', 'get_throttled'], capture_output=True,
                                text=True, timeout=2, check=True)
        return int(result.stdout.strip().split('=')[1], 16)
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return None


def temperature():
    try:
        return int(Path('/sys/class/thermal/thermal_zone0/temp').read_text()) / 1000
    except (OSError, ValueError):
        return None


def measure(pid, seconds):
    ticks_per_second = os.sysconf('SC_CLK_TCK')
    previous = family(processes(), pid)
    if not previous:
        raise ValueError('Publisher PID no longer exists')
    identity = previous[pid]['start']
    previous_time = started = time.monotonic()
    rows = []
    initial_flags = throttle()
    while time.monotonic() - started < seconds:
        time.sleep(min(1, max(0, seconds - (time.monotonic() - started))))
        snapshot = processes()
        if pid not in snapshot or snapshot[pid]['start'] != identity:
            raise ValueError('Publisher stopped during measurement; repeat after it restarts')
        current = family(snapshot, pid)
        now = time.monotonic()
        interval = now - previous_time
        items = []
        for process_pid, entry in current.items():
            old = previous.get(process_pid)
            cpu = None
            if old and old['start'] == entry['start']:
                cpu = round(100 * (entry['ticks'] - old['ticks']) / ticks_per_second / interval, 2)
            items.append({'pid': process_pid, 'name': entry['name'],
                          'cpu_one_core_percent': cpu, 'rss_mib': round(entry['rss_mib'], 2)})
        flags = throttle()
        rows.append({'elapsed_seconds': round(now - started, 2),
                     'cpu_total_one_core_percent': round(sum(p['cpu_one_core_percent'] or 0 for p in items), 2),
                     'rss_sum_mib': round(sum(p['rss_mib'] for p in items), 2),
                     'temperature_c': temperature(),
                     'throttled': None if flags is None else hex(flags),
                     'active_power_or_thermal_flags': None if flags is None else bool(flags & 0xF),
                     'processes': items})
        previous, previous_time = current, now
    flags = [row['active_power_or_thermal_flags'] for row in rows]
    temperatures = [row['temperature_c'] for row in rows if row['temperature_c'] is not None]
    summary = {
        'publisher_pid': pid, 'samples': len(rows),
        'cpu_average_one_core_percent': round(statistics.mean(row['cpu_total_one_core_percent'] for row in rows), 2),
        'cpu_peak_one_core_percent': max(row['cpu_total_one_core_percent'] for row in rows),
        'rss_sum_peak_mib': max(row['rss_sum_mib'] for row in rows),
        'temperature_peak_c': max(temperatures) if temperatures else None,
        'initial_throttled': None if initial_flags is None else hex(initial_flags),
        'active_flag_samples': sum(flag is True for flag in flags),
        'unavailable_flag_samples': sum(flag is None for flag in flags),
        'notes': ['100% CPU equals one fully occupied logical CPU; includes publisher and children.',
                  'RSS sums can double-count shared memory. This is not PSS.',
                  'No FPS, decode quality or end-to-end latency is measured here.',
                  'Compare equal camera settings, viewer count and network route.'],
    }
    return {'recorded_at_utc': datetime.now(timezone.utc).isoformat(), 'summary': summary, 'samples': rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, help='Default: discover the single dt-cam publisher')
    parser.add_argument('--seconds', type=int, default=60, choices=range(2, 3601), metavar='2..3600')
    parser.add_argument('--output', required=True, help='New JSON file under artifacts/')
    args = parser.parse_args()
    try:
        target = Path(args.output)
        # Reserve output with private permissions and reject overwrites before sampling.
        with os.fdopen(os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as file:
            result = measure(args.pid if args.pid is not None else discover(), args.seconds)
            json.dump(result, file, indent=2)
            file.write('\n')
        print(json.dumps(result['summary'], indent=2))
        print(f'Report: {target}')
    except (ValueError, OSError) as exc:
        parser.exit(1, f'{type(exc).__name__}: {exc}\n')


if __name__ == '__main__':
    main()
