"""Manual hardware check, run only after joining the trial SoftAP.

No packages, flashing, serial or measurement access. Connects solely to /ws.
"""
import base64
import hashlib
import json
import os
import socket
import struct


def receive_exact(sock, count):
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise RuntimeError("connection closed before complete response")
        data += chunk
    return data


def connect():
    sock = socket.create_connection(("192.168.4.1", 80), timeout=5)
    key = base64.b64encode(os.urandom(16)).decode("ascii")
    request = ("GET /ws HTTP/1.1\r\nHost: 192.168.4.1\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
               f"Sec-WebSocket-Key: {key}\r\n\r\n")
    sock.sendall(request.encode("ascii"))
    response = b""
    while not response.endswith(b"\r\n\r\n"):
        response += receive_exact(sock, 1)
        if len(response) > 8192:
            raise RuntimeError("oversized HTTP response")
    return sock, key, response.decode("ascii")


def receive_text(sock):
    while True:
        header = receive_exact(sock, 2)
        opcode, size = header[0] & 15, header[1] & 127
        if header[1] & 128:
            raise RuntimeError("server sent masked frame")
        if size == 126:
            size = struct.unpack("!H", receive_exact(sock, 2))[0]
        elif size == 127:
            size = struct.unpack("!Q", receive_exact(sock, 8))[0]
        if size > 1024:
            raise RuntimeError("oversized trial response")
        payload = receive_exact(sock, size)
        if opcode == 9:
            send_frame(sock, 10, payload)
            continue
        if opcode != 1 or not header[0] & 128:
            raise RuntimeError(f"expected final text frame, got opcode {opcode}")
        return payload.decode("utf-8")


def send_frame(sock, opcode, payload):
    mask = os.urandom(4)
    length = len(payload)
    header = bytes([0x80 | opcode, 0x80 | length]) if length < 126 else bytes([0x80 | opcode, 0x80 | 126]) + struct.pack("!H", length)
    masked = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    sock.sendall(header + mask + masked)


def main():
    first, key, response = connect()
    try:
        if not response.startswith("HTTP/1.1 101"):
            raise RuntimeError(response)
        expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode("ascii")).digest()).decode("ascii")
        # Header names are insensitive; the base64 value is case-sensitive.
        values = {line.split(":", 1)[0].lower(): line.split(":", 1)[1].strip()
                  for line in response.split("\r\n") if ":" in line}
        if values.get("sec-websocket-accept") != expected:
            raise RuntimeError("invalid WebSocket accept")
        hello = receive_text(first)
        assert json.loads(hello)["mode"] == "SIMULATED", hello
        message = "SIMULATED G0 echo"
        send_frame(first, 1, message.encode("utf-8"))
        assert receive_text(first) == message
        second, _, rejected = connect()
        second.close()
        assert not rejected.startswith("HTTP/1.1 101"), "second session accepted"
        send_frame(first, 8, struct.pack("!H", 1000))
        print("PASS SIMULATED hello; exact UTF-8 echo; second WebSocket rejected")
    finally:
        first.close()


if __name__ == "__main__":
    main()
