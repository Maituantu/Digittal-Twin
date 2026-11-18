"""Drop MQTT traffic without closing client TCP, then verify keepalive recovery."""
import os
import pathlib
import queue
import select
import socket
import subprocess
import sys
import tempfile
import threading
import time


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


driver, mosquitto = sys.argv[1:]
broker_port = free_port()
drop = threading.Event()
stopping = threading.Event()
output = queue.Queue()
app = broker = None
listener = socket.socket()
listener.bind(("127.0.0.1", 0))
proxy_port = listener.getsockname()[1]
listener.listen()
listener.settimeout(0.2)


def relay(client):
    upstream = None
    try:
        upstream = socket.create_connection(("127.0.0.1", broker_port), timeout=2)
        sockets = [client, upstream]
        while not stopping.is_set() and client in sockets:
            readable, _, _ = select.select(sockets, [], [], 0.1)
            for source in readable:
                data = source.recv(8192)
                if not data:
                    sockets.remove(source)
                    # Keep downstream open even if the broker times out upstream.
                    continue
                if not drop.is_set():
                    target = upstream if source is client else client
                    if target in sockets:
                        target.sendall(data)
    except OSError:
        pass
    finally:
        client.close()
        if upstream is not None:
            upstream.close()


def accept_connections():
    while not stopping.is_set():
        try:
            client, _ = listener.accept()
            threading.Thread(target=relay, args=(client,), daemon=True).start()
        except socket.timeout:
            continue
        except OSError:
            return


def read_output():
    for line in app.stdout:
        output.put(line.rstrip())


def wait_line(text, timeout):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        try:
            line = output.get(timeout=0.2)
        except queue.Empty:
            continue
        lines.append(line)
        if text.lower() in line.lower():
            return
    raise AssertionError(f"Missing {text!r}: {lines}")


with tempfile.TemporaryDirectory() as directory:
    config = pathlib.Path(directory) / "broker.conf"
    config.write_text(f"listener {broker_port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
    try:
        broker = subprocess.Popen([mosquitto, "-c", str(config)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                with socket.create_connection(("127.0.0.1", broker_port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        threading.Thread(target=accept_connections, daemon=True).start()
        environment = {key: value for key, value in os.environ.items()
                       if key not in ("MQTT_USERNAME", "MQTT_PASSWORD")}
        environment.update(MQTT_TEST_HOST="127.0.0.1", MQTT_TEST_PORT=str(proxy_port))
        app = subprocess.Popen([driver], env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
        threading.Thread(target=read_output, daemon=True).start()
        wait_line("connected=1 ready=1", 10)
        started = time.monotonic()
        drop.set()
        wait_line("keepalive", 40)
        wait_line("connected=0 ready=0", 5)
        print(f"PASS: silent packet loss detected after {time.monotonic() - started:.1f}s", flush=True)
        drop.clear()
        wait_line("connected=1 ready=1", 25)
        print("PASS: network restored, automatically reconnected and resubscribed", flush=True)
    finally:
        stopping.set()
        listener.close()
        for process in (app, broker):
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=8)
