#!/usr/bin/env python3
"""Minimal TCP smoke test for kv-server (RESP over plain socket).

Usage (from repo root, after building kv-server):
  KV_PORT=6380 ./scripts/smoke_kvserver.py

Environment:
  KV_SERVER_BIN   path to kv-server (default: ./build/bin/kv-server)
  KV_PORT         listen port (default: 6379)
  KV_SERVER_HOST  default: 127.0.0.1
"""
from __future__ import annotations

import os
import subprocess
import socket
import sys
import time


def recv_all(sock: socket.socket, n: int = 65536) -> bytes:
    sock.settimeout(3.0)
    return sock.recv(n)


def main() -> int:
    host = os.environ.get("KV_SERVER_HOST", "127.0.0.1")
    port = int(os.environ.get("KV_PORT", "6379"))
    bin_path = os.environ.get("KV_SERVER_BIN", "./build/bin/kv-server")

    if not os.path.isfile(bin_path) or not os.access(bin_path, os.X_OK):
        print(f"missing or not executable: {bin_path}", file=sys.stderr)
        return 2

    proc = subprocess.Popen([bin_path])
    try:
        time.sleep(0.5)
        sock = socket.create_connection((host, port), timeout=3.0)
        try:
            sock.sendall(b"*1\r\n$4\r\nPING\r\n")
            data = recv_all(sock)
            assert b"PONG" in data, data

            sock.sendall(b"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n")
            data = recv_all(sock)
            assert b"OK" in data or b"+OK" in data, data

            sock.sendall(b"*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n")
            data = recv_all(sock)
            assert b"bar" in data, data
        finally:
            sock.close()
        print("smoke_kvserver.py: OK")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
