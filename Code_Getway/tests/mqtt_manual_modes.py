"""Build and exercise all three manual test modes on an isolated local broker."""
import json
import pathlib
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time


compiler, source_dir, mqtt_library, broker_binary = sys.argv[1:]
source_dir = pathlib.Path(source_dir)


def packet(header, body):
    length = len(body)
    encoded = bytearray()
    while True:
        byte = length % 128
        length //= 128
        encoded.append(byte | (128 if length else 0))
        if not length:
            return bytes([header]) + encoded + body


def string(value):
    data = value.encode()
    return len(data).to_bytes(2, "big") + data


def read_exact(client, count):
    data = b""
    while len(data) < count:
        part = client.recv(count - len(data))
        if not part:
            raise AssertionError("MQTT connection closed")
        data += part
    return data


def read_packet(client):
    header = read_exact(client, 1)[0]
    size, multiplier = 0, 1
    while True:
        byte = read_exact(client, 1)[0]
        size += (byte & 127) * multiplier
        multiplier *= 128
        if not byte & 128:
            return header, read_exact(client, size)


def connect_observer(port):
    client = socket.create_connection(("127.0.0.1", port), timeout=5)
    client.sendall(packet(0x10, string("MQTT") + b"\x04\x02\x00\x3c" + string("mode-observer")))
    assert read_packet(client) == (0x20, b"\x00\x00")
    client.sendall(packet(0x82, b"\x00\x01" + string("test/pi-kit01/#") + b"\x00"))
    assert read_packet(client) == (0x90, b"\x00\x01\x00")
    return client


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        process.wait(timeout=5)


class Output:
    def __init__(self, process):
        self.lines = []
        self.queue = queue.Queue()
        self.thread = threading.Thread(target=self.read, args=(process,), daemon=True)
        self.thread.start()

    def read(self, process):
        for line in process.stdout:
            self.lines.append(line.rstrip())
            self.queue.put(line.rstrip())

    def wait(self, text, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.queue.get(timeout=0.1)
            except queue.Empty:
                continue
            if text in line:
                return line
        raise AssertionError(f"Missing {text!r}: {self.lines}")


with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    broker = app = None
    observer = None
    try:
        configuration = temporary / "broker.conf"
        configuration.write_text(f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n")
        broker = subprocess.Popen([broker_binary, "-c", str(configuration)],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.05)
        sources = [source_dir / "main.cpp", source_dir / "system/sys_manager.cpp", source_dir / "common/command_queue.cpp", source_dir / "uart/uart_client.cpp", source_dir / "uart/uart_messages.cpp", source_dir / "uart/bts/bts_frame_message.c", source_dir / "uart/bts/bts_convert.c", source_dir / "test/mqtt_test.cpp",
                   source_dir / "test/mqtt_test_client.cpp", source_dir / "test/mqtt_test_messages.cpp"]
        for mode in (1, 2, 3):
            executable = temporary / f"mode-{mode}"
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic",
                            "-DMAIN_USE_MQTT_TEST=1", "-DMAIN_USE_UART_TEST=0", f"-DMQTT_TEST_MODE={mode}", '-DMQTT_TEST_HOST="127.0.0.1"',
                            f"-DMQTT_TEST_PORT={port}", "-I", str(source_dir),
                            *map(str, sources), mqtt_library, "-lmosquitto", "-ljansson", "-pthread",
                            "-o", str(executable)], check=True, timeout=30)
            observer = connect_observer(port)
            app = subprocess.Popen([str(executable)], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True)
            output = Output(app)
            if mode == 2:
                output.wait("Local message examples passed")
                time.sleep(0.6)
                assert sum("Command decoded:" in line for line in output.lines) == 6
                assert not any("[MQTT] Connecting" in line for line in output.lines)
                assert not any("MQTT worker started" in line for line in output.lines)
                assert sum("LOCAL synthetic telemetry" in line for line in output.lines) == 1
            else:
                topic_payloads = {}
                for _ in range(1 if mode == 1 else 2):
                    header, body = read_packet(observer)
                    assert header == 0x30
                    topic_length = int.from_bytes(body[:2], "big")
                    topic = body[2:2 + topic_length].decode()
                    topic_payloads[topic] = json.loads(body[2 + topic_length:])
                assert topic_payloads["test/pi-kit01/status"] == {"status": "mqtt_test_ready"}
                if mode == 3:
                    telemetry = topic_payloads["test/pi-kit01/telemetry"]
                    assert telemetry["kit_code"] == "KIT01"
                    assert telemetry["current_ma"] == 125.5
                    assert telemetry["scale"] == "199.9"
                    assert telemetry["ts"].endswith("Z")
                observer.sendall(packet(0x30, string("lab/kit/KIT01/cmd/voltage") + b'{"voltage":9}'))
                output.wait("Command received:")
                if mode == 3:
                    output.wait("Command decoded: id=0x03 voltage_v=9")
                else:
                    time.sleep(0.3)
                    assert not any("Command decoded:" in line for line in output.lines)
            stop(app)
            output.thread.join(timeout=1)
            assert app.returncode == 0, output.lines
            observer.close()
            observer = None
            print(f"PASS mode={mode}: config/run dispatch, selected behavior and clean stop", flush=True)
    finally:
        if observer is not None:
            observer.close()
        stop(app)
        stop(broker)
