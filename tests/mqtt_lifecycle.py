"""Exercise the client against a loopback-only broker, never external hosts."""
import os
import pathlib
import select
import signal
import socket
import subprocess
import sys
import tempfile
import time


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        process.wait(timeout=8)


def wait_text(process, expected, timeout=12):
    received = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            data = os.read(process.stdout.fileno(), 8192)
            if not data:
                break
            received += data
            if expected.encode() in received:
                return
    raise AssertionError(f"Missing {expected!r}; received {received!r}")


def publish(port, topic, retained=False):
    topic = topic.encode()
    payload = b'{"action":"on"}'
    body = len(topic).to_bytes(2, "big") + topic + payload
    assert len(body) < 128
    with socket.create_connection(("127.0.0.1", port), timeout=2) as client:
        client.sendall(b'\x10\x10\x00\x04MQTT\x04\x02\x00\x0a\x00\x04test')
        assert client.recv(4) == b'\x20\x02\x00\x00'
        client.sendall(bytes([0x31 if retained else 0x30, len(body)]) + body)
        time.sleep(0.15)
        client.sendall(b'\xe0\x00')


gateway, broker_binary, generic_test = sys.argv[1:]
with socket.socket() as reservation:
    reservation.bind(("127.0.0.1", 0))
    port = reservation.getsockname()[1]

broker = app = None
with tempfile.TemporaryDirectory() as directory:
    config = pathlib.Path(directory) / "mosquitto.conf"
    config.write_text(f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n")

    def start_broker():
        process = subprocess.Popen([broker_binary, "-c", str(config)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    return process
            except OSError:
                time.sleep(0.1)
        stop(process)
        raise AssertionError("Broker failed to start")

    def start_app():
        environment = {key: value for key, value in os.environ.items()
                       if key not in ("MQTT_USERNAME", "MQTT_PASSWORD", "MQTT_CA_FILE")}
        environment["MQTT_TEST_PORT"] = str(port)
        environment["MQTT_TEST_HOST"] = "127.0.0.1"
        return subprocess.Popen([gateway],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment)

    try:
        app = start_app()
        wait_text(app, "worker started")
        time.sleep(0.5)  # First connection fails: broker is deliberately absent.
        broker = start_broker()
        wait_text(app, "connected=1 ready=1")
        subprocess.run([generic_test], env={**os.environ, "MQTT_TEST_PORT": str(port)}, check=True, timeout=20)
        publish(port, "lab/kit/KIT01/cmd/power")
        wait_text(app, "Command received")
        publish(port, "lab/kit/OTHER/cmd/power")
        time.sleep(0.3)
        assert not select.select([app.stdout], [], [], 0)[0], "Cross-kit command delivered"
        stop(broker)
        wait_text(app, "connected=0 ready=0")
        broker = start_broker()
        wait_text(app, "connected=1 ready=1")
        publish(port, "lab/kit/KIT01/cmd/power")
        wait_text(app, "Command received")
        stop(app)
        assert app.returncode == 0
        publish(port, "lab/kit/KIT01/cmd/power", retained=True)
        app = start_app()
        wait_text(app, "connected=1 ready=1")
        time.sleep(0.3)
        assert not select.select([app.stdout], [], [], 0)[0], "Retained command delivered"
        stop(broker)
        wait_text(app, "connected=0 ready=0")
        stop(app)
        assert app.returncode == 0
        print("PASS: offline start, SUBACK ready, commands, kit isolation, reconnect, retained drop, offline stop")
    finally:
        stop(app)
        stop(broker)
