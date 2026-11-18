import os
import pty
import select
import subprocess
import sys
import time

def crc(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0xa001 if value & 1 else 0)
    return value.to_bytes(2, "little")

master, slave = pty.openpty()
try:
    app = subprocess.Popen([sys.argv[1], os.ttyname(slave)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        received = b""
        deadline = time.monotonic() + 3
        while len(received) < 6 and time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                received += os.read(master, 256)
        body = bytes([3, 1, 9])
        assert received == b"\xaa" + body + crc(body), received.hex()
        body = bytes([0x80, 3, 0, 3, 0])
        reply = b"\xaa" + body + crc(body)
        os.write(master, b"noise" + reply[:2])
        time.sleep(0.02)
        os.write(master, reply[2:])
        output, error = app.communicate(timeout=4)
        assert app.returncode == 0, (app.returncode, output, error)
        print(output.decode(), end="")
    finally:
        if app.poll() is None:
            app.kill()
            app.wait()
finally:
    os.close(master)
    os.close(slave)
