"""Compare network-worker CPU/context switches of two builds on local brokers.

Usage: python3 mqtt_idle_profile.py BEFORE_DRIVER AFTER_DRIVER MOSQUITTO
Reads Linux /proc counters; results are host measurements, not Pi benchmarks.
"""
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


def port():
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


def counters(pid):
    cpu = switches = 0
    for task in pathlib.Path(f"/proc/{pid}/task").iterdir():
        if task.name == str(pid):
            continue
        cpu += int((task / "schedstat").read_text().split()[0])
        for line in (task / "status").read_text().splitlines():
            if line.startswith(("voluntary_ctxt_switches:", "nonvoluntary_ctxt_switches:")):
                switches += int(line.split()[1])
    return cpu, switches


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        process.wait(timeout=8)


before, after, broker_binary = sys.argv[1:]
for mode in ("connected-idle", "offline-retry"):
    apps, brokers = [], []
    with tempfile.TemporaryDirectory() as directory:
        try:
            for index, executable in enumerate((before, after)):
                mqtt_port = port()
                if mode == "connected-idle":
                    config = pathlib.Path(directory) / f"broker-{index}.conf"
                    config.write_text(f"listener {mqtt_port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
                    broker = subprocess.Popen([broker_binary, "-c", str(config)],
                                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    brokers.append(broker)
                    time.sleep(0.2)
                environment = {key: value for key, value in os.environ.items()
                               if key not in ("MQTT_USERNAME", "MQTT_PASSWORD")}
                environment.update(MQTT_TEST_HOST="127.0.0.1", MQTT_TEST_PORT=str(mqtt_port))
                apps.append(subprocess.Popen([executable], env=environment,
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            time.sleep(2)
            baseline = [counters(app.pid) for app in apps]
            start = time.monotonic()
            time.sleep(6)
            elapsed = time.monotonic() - start
            for label, app, initial in zip(("before", "after"), apps, baseline):
                final = counters(app.pid)
                cpu_ms = (final[0] - initial[0]) / 1e6
                print(f"{mode} {label}: worker CPU={cpu_ms:.3f}ms over {elapsed:.1f}s; "
                      f"context switches={final[1] - initial[1]}", flush=True)
            for label, app in zip(("before", "after"), apps):
                started = time.monotonic()
                stop(app)
                print(f"{mode} {label}: stop={time.monotonic() - started:.3f}s", flush=True)
        finally:
            for app in apps:
                stop(app)
            for broker in brokers:
                stop(broker)
