#!/usr/bin/env python3
"""Loopback-only SRS on_dvr receipt adapter for the opt-in DVR PoC."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import stat
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any, Callable, Mapping
import uuid


SCHEMA_VERSION = 1
MAX_REQUEST_BYTES = 64 * 1024
MAX_RECEIPTS_TO_SCAN = 50_000
DEFAULT_MINIMUM_FREE_BYTES = 2 * 1024 * 1024 * 1024
RESOURCE_COMPONENT = re.compile(r"^[A-Za-z0-9_-]{1,128}$")
TIMESTAMP_MS = re.compile(r"(?<!\d)(1\d{12})(?!\d)")


class ReceiptError(Exception):
    def __init__(self, message: str, http_status: int = 400) -> None:
        super().__init__(message)
        self.http_status = http_status


class ReceiptStoreBlocked(ReceiptError):
    pass


def utc_now_text() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _inside(child: Path, parent: Path) -> bool:
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def _safe_optional_text(payload: Mapping[str, Any], key: str) -> str | None:
    value = payload.get(key)
    if value is None:
        return None
    if not isinstance(value, str) or len(value) > 256:
        raise ReceiptError(f"invalid {key}")
    return value


class ReceiptStore:
    """Owns the receipt spool and its restart-safe in-memory deduplication index."""

    def __init__(
        self,
        dvr_root: Path,
        spool_root: Path,
        ffprobe: str = "ffprobe",
        minimum_free_bytes: int = DEFAULT_MINIMUM_FREE_BYTES,
        disk_usage: Callable[[Path], Any] = shutil.disk_usage,
        probe_timeout_seconds: float = 5.0,
    ) -> None:
        if minimum_free_bytes < 0:
            raise ValueError("minimum_free_bytes must not be negative")
        if not dvr_root.expanduser().is_absolute() or not spool_root.expanduser().is_absolute():
            raise ReceiptStoreBlocked("DVR and spool roots must be explicit absolute paths", 500)
        self.dvr_root = dvr_root.expanduser().resolve(strict=True)
        if not self.dvr_root.is_dir():
            raise ReceiptStoreBlocked("DVR root is not a directory", 500)
        self.spool_root = spool_root.expanduser().resolve(strict=False)
        self.incoming_root = self.spool_root / "incoming"
        self.receipts_root = self.spool_root / "receipts"
        self.incoming_root.mkdir(parents=True, exist_ok=True)
        self.receipts_root.mkdir(parents=True, exist_ok=True)
        self.ffprobe = ffprobe
        self.minimum_free_bytes = minimum_free_bytes
        self.disk_usage = disk_usage
        self.probe_timeout_seconds = probe_timeout_seconds
        self._dedup: set[tuple[str, int, int]] = set()
        self._lock = threading.Lock()
        self._load_existing_receipts()

    @property
    def receipt_count(self) -> int:
        return len(self._dedup)

    def _load_existing_receipts(self) -> None:
        receipt_paths = sorted(self.receipts_root.glob("*.json"))
        if len(receipt_paths) > MAX_RECEIPTS_TO_SCAN:
            raise ReceiptStoreBlocked("receipt scan limit exceeded", 500)
        for path in receipt_paths:
            try:
                record = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as exc:
                raise ReceiptStoreBlocked(f"invalid existing receipt: {path.name}", 500) from exc
            if not isinstance(record, dict) or record.get("schemaVersion") != SCHEMA_VERSION:
                raise ReceiptStoreBlocked(f"unsupported existing receipt: {path.name}", 500)
            try:
                key = (
                    str(record["relativePath"]),
                    int(record["sizeBytes"]),
                    int(record["mtimeNs"]),
                )
            except (KeyError, TypeError, ValueError) as exc:
                raise ReceiptStoreBlocked(f"incomplete existing receipt: {path.name}", 500) from exc
            self._dedup.add(key)

    def _validate_payload(self, payload: Mapping[str, Any]) -> tuple[dict[str, Any], Path, tuple[str, int, int]]:
        if payload.get("action") != "on_dvr":
            raise ReceiptError("unsupported action")
        app = payload.get("app")
        stream = payload.get("stream")
        if not isinstance(app, str) or not RESOURCE_COMPONENT.fullmatch(app):
            raise ReceiptError("invalid app")
        if not isinstance(stream, str) or not RESOURCE_COMPONENT.fullmatch(stream):
            raise ReceiptError("invalid stream")
        raw_file = payload.get("file")
        if not isinstance(raw_file, str) or not raw_file or len(raw_file) > 4096:
            raise ReceiptError("invalid file")
        callback_path = Path(raw_file)
        if not callback_path.is_absolute() or ".." in callback_path.parts:
            raise ReceiptError("file must be an absolute path without traversal")
        try:
            canonical = callback_path.resolve(strict=True)
        except OSError as exc:
            raise ReceiptError("DVR file does not exist", 404) from exc
        if not _inside(canonical, self.dvr_root):
            raise ReceiptError("DVR file is outside configured root", 403)
        try:
            file_stat = canonical.stat()
        except OSError as exc:
            raise ReceiptError("DVR file cannot be inspected", 404) from exc
        if not stat.S_ISREG(file_stat.st_mode) or canonical.suffix.lower() != ".flv":
            raise ReceiptError("DVR file is not a regular FLV file")
        relative_path = canonical.relative_to(self.dvr_root).as_posix()
        key = (relative_path, int(file_stat.st_size), int(file_stat.st_mtime_ns))
        safe = {
            "serverId": _safe_optional_text(payload, "server_id"),
            "serviceId": _safe_optional_text(payload, "service_id"),
            "streamId": _safe_optional_text(payload, "stream_id"),
            "vhost": _safe_optional_text(payload, "vhost"),
            "app": app,
            "stream": stream,
            "relativePath": relative_path,
            "sizeBytes": key[1],
            "mtimeNs": key[2],
        }
        return safe, canonical, key

    def _check_free_space(self) -> None:
        usage = self.disk_usage(self.spool_root)
        threshold = max(self.minimum_free_bytes, int(usage.total * 0.05))
        if usage.free < threshold:
            raise ReceiptError("insufficient free space for receipt", 507)

    def _probe(self, path: Path) -> tuple[str, int | None]:
        command = [
            self.ffprobe,
            "-v",
            "error",
            "-show_entries",
            "format=duration",
            "-of",
            "default=noprint_wrappers=1:nokey=1",
            os.fspath(path),
        ]
        try:
            result = subprocess.run(
                command,
                check=False,
                capture_output=True,
                text=True,
                timeout=self.probe_timeout_seconds,
            )
        except FileNotFoundError:
            return "executable_not_found", None
        except subprocess.TimeoutExpired:
            return "timeout", None
        except OSError:
            return "launch_failed", None
        if result.returncode != 0:
            return "probe_failed", None
        try:
            seconds = float(result.stdout.strip())
        except ValueError:
            return "invalid_duration", None
        if seconds < 0:
            return "invalid_duration", None
        return "ok", round(seconds * 1000)

    @staticmethod
    def _segment_started_at(path: Path) -> str | None:
        match = TIMESTAMP_MS.search(path.stem)
        if not match:
            return None
        try:
            value = dt.datetime.fromtimestamp(int(match.group(1)) / 1000.0, tz=dt.timezone.utc)
        except (OverflowError, OSError, ValueError):
            return None
        return value.isoformat(timespec="milliseconds").replace("+00:00", "Z")

    def record(self, payload: Mapping[str, Any]) -> tuple[bool, dict[str, Any] | None]:
        if not isinstance(payload, Mapping):
            raise ReceiptError("request body must be a JSON object")
        with self._lock:
            safe, canonical, key = self._validate_payload(payload)
            if key in self._dedup:
                return False, None
            self._check_free_space()
            probe_status, duration_ms = self._probe(canonical)
            receipt_id = str(uuid.uuid4())
            receipt: dict[str, Any] = {
                "schemaVersion": SCHEMA_VERSION,
                "receiptId": receipt_id,
                "receivedAtUtc": utc_now_text(),
                **safe,
                "probeStatus": probe_status,
                "sourceAssurance": "unverified-local-callback",
                "contentHashVerification": "not_performed",
                "deduplication": "relative_path+size+mtime_ns",
            }
            started_at = self._segment_started_at(canonical)
            if started_at is not None:
                receipt["segmentStartedAtUtc"] = started_at
            if duration_ms is not None:
                receipt["durationMs"] = duration_ms
            published_name = f"{dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')}-{receipt_id}.json"
            temporary = self.incoming_root / f"{receipt_id}.tmp"
            published = self.receipts_root / published_name
            try:
                serialized = json.dumps(receipt, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
                with temporary.open("x", encoding="utf-8", newline="\n") as output:
                    output.write(serialized)
                    output.write("\n")
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(temporary, published)
                self._fsync_directory(self.receipts_root)
            except OSError as exc:
                try:
                    temporary.unlink(missing_ok=True)
                except OSError:
                    pass
                raise ReceiptError("atomic receipt commit failed", 500) from exc
            self._dedup.add(key)
            return True, receipt

    @staticmethod
    def _fsync_directory(path: Path) -> None:
        if not hasattr(os, "O_DIRECTORY"):
            return
        try:
            descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        except OSError:
            return
        try:
            os.fsync(descriptor)
        except OSError:
            pass
        finally:
            os.close(descriptor)


class ReceiptRequestHandler(BaseHTTPRequestHandler):
    server_version = "RtmpMonitorDvrReceipt/1"
    sys_version = ""

    def do_POST(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        if self.path != "/api/v1/dvrs":
            self._reply(404, b"not found\n")
            return
        raw_length = self.headers.get("Content-Length")
        try:
            length = int(raw_length) if raw_length is not None else -1
        except ValueError:
            length = -1
        if length < 0:
            self._reply(411, b"content length required\n")
            return
        if length > MAX_REQUEST_BYTES:
            self._reply(413, b"request too large\n")
            return
        body = self.rfile.read(length)
        try:
            payload = json.loads(body.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            self._reply(400, b"invalid json\n")
            return
        try:
            created, _receipt = self.server.receipt_store.record(payload)  # type: ignore[attr-defined]
        except ReceiptError as exc:
            self.server.log_adapter_event("receipt_rejected", str(exc))  # type: ignore[attr-defined]
            self._reply(exc.http_status, b"receipt rejected\n")
            return
        self.server.log_adapter_event("receipt_created" if created else "receipt_duplicate", "ok")  # type: ignore[attr-defined]
        self._reply(200, b"0")

    def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler contract
        if self.path == "/healthz":
            self._reply(200, b"ok\n")
        else:
            self._reply(405, b"method not allowed\n")

    def _reply(self, status: int, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, _format: str, *_args: Any) -> None:
        return


class ReceiptHttpServer(HTTPServer):
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], receipt_store: ReceiptStore) -> None:
        super().__init__(address, ReceiptRequestHandler)
        self.receipt_store = receipt_store
        self.timeout = 0.5

    @staticmethod
    def log_adapter_event(event: str, detail: str) -> None:
        print(json.dumps({"event": event, "detail": detail}, ensure_ascii=False), file=sys.stderr, flush=True)


def _is_loopback(host: str) -> bool:
    try:
        addresses = {item[4][0] for item in socket.getaddrinfo(host, None)}
    except socket.gaierror:
        return False
    return bool(addresses) and all(address.startswith("127.") or address == "::1" for address in addresses)


def serve(arguments: argparse.Namespace) -> int:
    if not _is_loopback(arguments.listen):
        print("receipt adapter refuses non-loopback listen address", file=sys.stderr)
        return 2
    try:
        store = ReceiptStore(
            Path(arguments.dvr_root),
            Path(arguments.spool_root),
            ffprobe=arguments.ffprobe,
            minimum_free_bytes=arguments.minimum_free_bytes,
            probe_timeout_seconds=arguments.probe_timeout_seconds,
        )
        server = ReceiptHttpServer((arguments.listen, arguments.port), store)
    except (OSError, ReceiptError, ValueError) as exc:
        print(f"receipt adapter startup blocked: {exc}", file=sys.stderr)
        return 2
    stopping = threading.Event()

    def request_stop(_signum: int, _frame: Any) -> None:
        stopping.set()

    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    print(json.dumps({"event": "adapter_ready", "listen": arguments.listen, "port": arguments.port}), flush=True)
    try:
        while not stopping.is_set():
            server.handle_request()
    finally:
        server.server_close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    run = subcommands.add_parser("serve", help="run the loopback callback adapter")
    run.add_argument("--listen", default="127.0.0.1")
    run.add_argument("--port", type=int, default=18085)
    run.add_argument("--dvr-root", required=True)
    run.add_argument("--spool-root", required=True)
    run.add_argument("--ffprobe", default="ffprobe")
    run.add_argument("--probe-timeout-seconds", type=float, default=5.0)
    run.add_argument("--minimum-free-bytes", type=int, default=DEFAULT_MINIMUM_FREE_BYTES)
    run.set_defaults(function=serve)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = build_parser().parse_args(argv)
    return int(arguments.function(arguments))


if __name__ == "__main__":
    raise SystemExit(main())
