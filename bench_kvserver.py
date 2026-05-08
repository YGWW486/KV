#!/usr/bin/env python3
"""
End-to-end benchmark for kv-server (RESP protocol over TCP).

Usage:
  ./scripts/bench_kvserver.py
  ./scripts/bench_kvserver.py --concurrency 50 --requests 100000 --pipeline 16
  KV_PORT=6380 KV_PAYLOAD=1024 ./scripts/bench_kvserver.py

Environment:
  KV_SERVER_BIN   path to kv-server (default: ./build/bin/kv-server)
  KV_PORT         listen port (default: 6380 to avoid Redis conflict)
  KV_SERVER_HOST  (default: 127.0.0.1)
  KV_PAYLOAD      value size in bytes for SET/GET (default: 64)
"""
from __future__ import annotations

import argparse
import os
import socket
import statistics
import subprocess
import sys
import threading
import time
from typing import List, Tuple


# ---------------------------------------------------------------------------
# Buffered socket reader (avoids 1-byte recv() syscall per RESP byte)
# ---------------------------------------------------------------------------

class BufSock:
    """Wrap a socket with an internal read buffer for efficient RESP parsing."""

    def __init__(self, sock: socket.socket):
        self._sock = sock
        self._buf = b""

    def _fill(self) -> None:
        chunk = self._sock.recv(65536)
        if not chunk:
            raise ConnectionError("connection closed")
        self._buf += chunk

    def _ensure(self, n: int) -> None:
        while len(self._buf) < n:
            self._fill()

    def read_exact(self, n: int) -> bytes:
        self._ensure(n)
        data = self._buf[:n]
        self._buf = self._buf[n:]
        return data

    def read_line(self) -> bytes:
        while b"\r\n" not in self._buf:
            self._fill()
        idx = self._buf.index(b"\r\n") + 2
        line = self._buf[:idx]
        self._buf = self._buf[idx:]
        return line


# ---------------------------------------------------------------------------
# RESP helpers
# ---------------------------------------------------------------------------

def resp_cmd(*parts: str) -> bytes:
    """Encode a RESP array command, e.g. resp_cmd('SET', 'k', 'v')."""
    header = f"*{len(parts)}\r\n".encode()
    body = b""
    for p in parts:
        b = p.encode()
        body += f"${len(b)}\r\n".encode() + b + b"\r\n"
    return header + body


def recv_resp(bs: BufSock) -> None:
    """Read and discard one RESP response from the buffered socket."""
    line = bs.read_line()
    prefix = line[0:1]
    if prefix in (b"+", b"-", b":"):
        return
    if prefix == b"$":
        length = int(line[1:-2])
        if length >= 0:
            bs.read_exact(length)
        bs.read_exact(2)  # trailing \r\n
        return
    if prefix == b"*":
        count = int(line[1:-2])
        if count >= 0:
            for _ in range(count):
                recv_resp(bs)
        return


# ---------------------------------------------------------------------------
# Benchmark runner
# ---------------------------------------------------------------------------

class BenchResult:
    def __init__(self, name: str, latencies_ms: List[float], total_sec: float):
        self.name = name
        self.count = len(latencies_ms)
        self.total_sec = total_sec
        self.qps = self.count / total_sec if total_sec > 0 else 0
        self.latencies_ms = sorted(latencies_ms)
        self.avg_ms = statistics.mean(latencies_ms) if latencies_ms else 0
        self.p50_ms = _pct(self.latencies_ms, 50)
        self.p99_ms = _pct(self.latencies_ms, 99)
        self.p999_ms = _pct(self.latencies_ms, 99.9)

    def print(self):
        print(f"  {self.name}:")
        print(f"    requests:  {self.count}")
        print(f"    qps:       {self.qps:,.0f}")
        print(f"    avg:       {self.avg_ms:.3f} ms")
        print(f"    p50:       {self.p50_ms:.3f} ms")
        print(f"    p99:       {self.p99_ms:.3f} ms")
        print(f"    p999:      {self.p999_ms:.3f} ms")


def _pct(sorted_data: List[float], pct: float) -> float:
    if not sorted_data:
        return 0.0
    k = (len(sorted_data) - 1) * pct / 100.0
    f = int(k)
    c = min(f + 1, len(sorted_data) - 1)
    if f == c:
        return sorted_data[f]
    return sorted_data[f] + (k - f) * (sorted_data[c] - sorted_data[f])


def run_pipelined(host: str, port: int, commands: List[bytes],
                  pipeline: int = 1, warmup: int = 0) -> BenchResult:
    """Send *commands* in batches of *pipeline*, measure per-batch RTT."""
    sock = socket.create_connection((host, port), timeout=10.0)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    try:
        bs = BufSock(sock)
        for _ in range(warmup):
            sock.sendall(commands[0])
            recv_resp(bs)

        latencies_ms: List[float] = []
        t0 = time.perf_counter()

        for i in range(0, len(commands), pipeline):
            batch = commands[i:i + pipeline]
            t_batch = time.perf_counter()
            sock.sendall(b"".join(batch))
            for _ in batch:
                recv_resp(bs)
            cmd_ms = (time.perf_counter() - t_batch) * 1000 / len(batch)
            latencies_ms.extend([cmd_ms] * len(batch))

        total_sec = time.perf_counter() - t0
    finally:
        sock.close()
    return BenchResult("", latencies_ms, total_sec)


def run_concurrent(host: str, port: int, commands: List[bytes],
                   concurrency: int, pipeline: int = 1) -> BenchResult:
    """Run *commands* split across *concurrency* threads, each with own socket."""
    per_thread = len(commands) // concurrency
    results_lock = threading.Lock()
    all_latencies: List[float] = []
    t0 = time.perf_counter()

    def worker(cmd_list: List[bytes]):
        sock = socket.create_connection((host, port), timeout=10.0)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            bs = BufSock(sock)
            latencies: List[float] = []
            for i in range(0, len(cmd_list), pipeline):
                batch = cmd_list[i:i + pipeline]
                t_batch = time.perf_counter()
                sock.sendall(b"".join(batch))
                for _ in batch:
                    recv_resp(bs)
                cmd_ms = (time.perf_counter() - t_batch) * 1000 / len(batch)
                latencies.extend([cmd_ms] * len(batch))
            with results_lock:
                all_latencies.extend(latencies)
        finally:
            sock.close()

    threads = []
    for t in range(concurrency):
        start = t * per_thread
        end = start + per_thread if t < concurrency - 1 else len(commands)
        th = threading.Thread(target=worker, args=(commands[start:end],))
        threads.append(th)
        th.start()

    for th in threads:
        th.join()

    total_sec = time.perf_counter() - t0
    return BenchResult("", all_latencies, total_sec)


def gen_payload(size: int) -> str:
    base = "X" * max(1, size - 30)
    return f"val_{base}"[:size]


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="kv-server end-to-end benchmark")
    parser.add_argument("--concurrency", "-c", type=int, default=1)
    parser.add_argument("--requests", "-n", type=int, default=100000)
    parser.add_argument("--pipeline", "-P", type=int, default=16)
    parser.add_argument("--phases", type=str, default="ping,set,get,mixed")
    parser.add_argument("--skip-server", action="store_true")
    args = parser.parse_args()

    host = os.environ.get("KV_SERVER_HOST", "127.0.0.1")
    port = int(os.environ.get("KV_PORT", "6380"))
    bin_path = os.environ.get("KV_SERVER_BIN", "./build/bin/kv-server")
    payload_size = int(os.environ.get("KV_PAYLOAD", "64"))

    proc = None
    if not args.skip_server:
        if not os.path.isfile(bin_path) or not os.access(bin_path, os.X_OK):
            print(f"ERROR: missing or not executable: {bin_path}", file=sys.stderr)
            return 2
        proc = subprocess.Popen([bin_path],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        time.sleep(0.5)

    try:
        try:
            sock = socket.create_connection((host, port), timeout=3.0)
            sock.close()
        except Exception:
            print(f"ERROR: cannot connect to {host}:{port}", file=sys.stderr)
            return 3

        phases = [p.strip() for p in args.phases.split(",")]
        val = gen_payload(payload_size)
        results: List[BenchResult] = []

        print(f"=== kv-server Benchmark ===")
        print(f"  host:         {host}:{port}")
        print(f"  requests:     {args.requests}")
        print(f"  pipeline:     {args.pipeline}")
        print(f"  payload:      {payload_size} bytes")
        print(f"  concurrency:  {args.concurrency}")
        print()

        if "ping" in phases:
            print("--- PING (latency probe, pipeline=1) ---")
            cmds = [resp_cmd("PING") for _ in range(min(args.requests, 10000))]
            r = run_pipelined(host, port, cmds, pipeline=1, warmup=100)
            r.name = "PING"
            results.append(r)
            r.print()
            print()

        if "set" in phases:
            print("--- SET ---")
            cmds = [resp_cmd("SET", f"bench:k{i:08d}", f"bench:v{i:08d}{val}")
                    for i in range(args.requests)]
            r = run_pipelined(host, port, cmds, pipeline=args.pipeline, warmup=100)
            r.name = "SET"
            results.append(r)
            r.print()
            print()

        if "get" in phases:
            print("--- GET ---")
            cmds = [resp_cmd("GET", f"bench:k{i:08d}")
                    for i in range(args.requests)]
            r = run_pipelined(host, port, cmds, pipeline=args.pipeline, warmup=100)
            r.name = "GET"
            results.append(r)
            r.print()
            print()

        if "mixed" in phases:
            print("--- Mixed (80% GET / 20% SET) ---")
            cmds = []
            for i in range(args.requests):
                if i % 5 == 0:
                    cmds.append(resp_cmd("SET", f"bench:m{i:08d}",
                                         f"bench:v{i:08d}{val}"))
                else:
                    cmds.append(resp_cmd("GET", f"bench:k{i % args.requests:08d}"))
            r = run_pipelined(host, port, cmds, pipeline=args.pipeline, warmup=100)
            r.name = "Mixed (80G/20S)"
            results.append(r)
            r.print()
            print()

        if "concurrent" in phases and args.concurrency > 1:
            print(f"--- Concurrent SET ({args.concurrency} connections) ---")
            cmds = [resp_cmd("SET", f"bench:cc{i:08d}", f"bench:v{i:08d}{val}")
                    for i in range(args.requests)]
            r = run_concurrent(host, port, cmds,
                               concurrency=args.concurrency,
                               pipeline=args.pipeline)
            r.name = f"SET (c={args.concurrency})"
            results.append(r)
            r.print()
            print()

        print("=" * 55)
        print("  Summary")
        print("=" * 55)
        for r in results:
            print(f"  {r.name:22s}  {r.qps:>12,.0f} QPS  p99={r.p99_ms:.3f}ms")
        print()

        return 0

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 4
    finally:
        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
