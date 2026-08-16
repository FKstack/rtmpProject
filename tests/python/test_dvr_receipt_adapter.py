#!/usr/bin/env python3

from __future__ import annotations

import http.client
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
ADAPTER_PATH = REPOSITORY_ROOT / "scripts" / "srs" / "dvr_receipt_adapter.py"
SPEC = importlib.util.spec_from_file_location("dvr_receipt_adapter", ADAPTER_PATH)
assert SPEC is not None and SPEC.loader is not None
adapter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(adapter)


class DiskUsage:
    def __init__(self, total: int = 1_000_000, free: int = 900_000) -> None:
        self.total = total
        self.used = total - free
        self.free = free


class ReceiptFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.dvr_root = self.root / "dvr"
        self.spool_root = self.root / "spool"
        self.segment = self.dvr_root / "live" / "camera_01" / "1710000000000.flv"
        self.segment.parent.mkdir(parents=True)
        self.segment.write_bytes(b"not-a-real-flv-for-unit-validation")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def store(self, **overrides):
        options = {
            "ffprobe": "definitely-missing-ffprobe-for-unit-test",
            "minimum_free_bytes": 0,
            "disk_usage": lambda _path: DiskUsage(),
        }
        options.update(overrides)
        return adapter.ReceiptStore(self.dvr_root, self.spool_root, **options)

    def payload(self, **overrides):
        value = {
            "action": "on_dvr",
            "server_id": "server-1",
            "service_id": "service-1",
            "stream_id": "stream-session-1",
            "vhost": "__defaultVhost__",
            "app": "live",
            "stream": "camera_01",
            "file": str(self.segment.resolve()),
            "tcUrl": "rtmp://user:secret@example.invalid/live?token=forbidden",
            "param": "token=forbidden",
            "ip": "203.0.113.10",
            "cwd": "/private/srs",
        }
        value.update(overrides)
        return value


class ReceiptStoreTests(ReceiptFixture):
    def test_valid_callback_creates_atomic_redacted_receipt(self) -> None:
        store = self.store()
        created, receipt = store.record(self.payload())
        self.assertTrue(created)
        self.assertIsNotNone(receipt)
        assert receipt is not None
        self.assertEqual(receipt["schemaVersion"], 1)
        self.assertEqual(receipt["relativePath"], "live/camera_01/1710000000000.flv")
        self.assertEqual(receipt["contentHashVerification"], "not_performed")
        self.assertEqual(receipt["deduplication"], "relative_path+size+mtime_ns")
        self.assertEqual(receipt["probeStatus"], "executable_not_found")
        self.assertIn("segmentStartedAtUtc", receipt)
        self.assertEqual(list((self.spool_root / "incoming").iterdir()), [])
        receipt_paths = list((self.spool_root / "receipts").glob("*.json"))
        self.assertEqual(len(receipt_paths), 1)
        stored_text = receipt_paths[0].read_text(encoding="utf-8")
        for forbidden in ("tcUrl", "param", "203.0.113.10", "token=forbidden", str(self.dvr_root.resolve())):
            self.assertNotIn(forbidden, stored_text)

    def test_store_requires_explicit_absolute_roots(self) -> None:
        with self.assertRaises(adapter.ReceiptStoreBlocked):
            adapter.ReceiptStore(Path("relative-dvr"), self.spool_root)
        with self.assertRaises(adapter.ReceiptStoreBlocked):
            adapter.ReceiptStore(self.dvr_root, Path("relative-spool"))

    def test_duplicate_and_restart_do_not_create_second_receipt(self) -> None:
        store = self.store()
        self.assertTrue(store.record(self.payload())[0])
        self.assertFalse(store.record(self.payload())[0])
        restarted = self.store()
        self.assertEqual(restarted.receipt_count, 1)
        self.assertFalse(restarted.record(self.payload())[0])
        self.assertEqual(len(list((self.spool_root / "receipts").glob("*.json"))), 1)

    def test_rejects_missing_fields_and_invalid_resource_characters(self) -> None:
        for payload in (
            self.payload(action="other"),
            self.payload(app="../live"),
            self.payload(stream="camera/01"),
            self.payload(file=""),
        ):
            with self.subTest(payload=payload):
                with self.assertRaises(adapter.ReceiptError):
                    self.store().record(payload)

    def test_rejects_traversal_outside_missing_and_non_regular_paths(self) -> None:
        outside = self.root / "outside.flv"
        outside.write_bytes(b"outside")
        directory = self.dvr_root / "directory.flv"
        directory.mkdir()
        traversal = str(self.segment.parent / ".." / "camera_01" / self.segment.name)
        candidates = [traversal, str(outside.resolve()), str(self.root / "missing.flv"), str(directory.resolve())]
        for candidate in candidates:
            with self.subTest(candidate=candidate):
                with self.assertRaises(adapter.ReceiptError):
                    self.store().record(self.payload(file=candidate))

    def test_rejects_symbolic_link_escape(self) -> None:
        outside = self.root / "outside.flv"
        outside.write_bytes(b"outside")
        link = self.dvr_root / "live" / "camera_01" / "escape.flv"
        try:
            link.symlink_to(outside)
        except OSError as exc:
            self.skipTest(f"symbolic links unavailable: {exc}")
        with self.assertRaises(adapter.ReceiptError):
            self.store().record(self.payload(file=str(link.absolute())))

    def test_low_disk_refuses_without_valid_receipt(self) -> None:
        store = self.store(minimum_free_bytes=901_000)
        with self.assertRaisesRegex(adapter.ReceiptError, "free space"):
            store.record(self.payload())
        self.assertEqual(list((self.spool_root / "receipts").glob("*.json")), [])

    def test_atomic_replace_failure_leaves_no_receipt_or_index(self) -> None:
        store = self.store()
        with mock.patch.object(adapter.os, "replace", side_effect=OSError("simulated")):
            with self.assertRaisesRegex(adapter.ReceiptError, "atomic"):
                store.record(self.payload())
        self.assertEqual(store.receipt_count, 0)
        self.assertEqual(list((self.spool_root / "receipts").glob("*.json")), [])
        self.assertEqual(list((self.spool_root / "incoming").iterdir()), [])

    def test_corrupt_or_higher_schema_receipt_blocks_restart_and_is_preserved(self) -> None:
        receipts = self.spool_root / "receipts"
        receipts.mkdir(parents=True)
        for name, content in (("corrupt.json", "{"), ("future.json", '{"schemaVersion":2}')):
            path = receipts / name
            path.write_text(content, encoding="utf-8")
            with self.subTest(name=name):
                with self.assertRaises(adapter.ReceiptStoreBlocked):
                    self.store()
                self.assertEqual(path.read_text(encoding="utf-8"), content)
            path.unlink()

    def test_probe_timeout_and_failure_are_explicit_without_blocking_receipt(self) -> None:
        store = self.store(ffprobe="ffprobe")
        with mock.patch.object(adapter.subprocess, "run", side_effect=subprocess.TimeoutExpired("ffprobe", 5)):
            _, timeout_receipt = store.record(self.payload())
        assert timeout_receipt is not None
        self.assertEqual(timeout_receipt["probeStatus"], "timeout")

        second = self.dvr_root / "live" / "camera_01" / "1710000010000.flv"
        second.write_bytes(b"second")
        failed = mock.Mock(returncode=1, stdout="", stderr="bad")
        with mock.patch.object(adapter.subprocess, "run", return_value=failed):
            _, failed_receipt = store.record(self.payload(file=str(second.resolve())))
        assert failed_receipt is not None
        self.assertEqual(failed_receipt["probeStatus"], "probe_failed")


class HttpAdapterTests(ReceiptFixture):
    def setUp(self) -> None:
        super().setUp()
        self.store_instance = self.store()
        self.server = adapter.ReceiptHttpServer(("127.0.0.1", 0), self.store_instance)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        super().tearDown()

    def request(self, body: bytes, headers=None, path="/api/v1/dvrs"):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=2)
        connection.request("POST", path, body=body, headers=headers or {"Content-Type": "application/json"})
        response = connection.getresponse()
        result = response.status, response.read()
        connection.close()
        return result

    def test_http_valid_duplicate_malformed_and_wrong_path(self) -> None:
        body = json.dumps(self.payload()).encode("utf-8")
        self.assertEqual(self.request(body), (200, b"0"))
        self.assertEqual(self.request(body), (200, b"0"))
        self.assertEqual(self.request(b"{"), (400, b"invalid json\n"))
        self.assertEqual(self.request(body, path="/other")[0], 404)
        self.assertEqual(len(list((self.spool_root / "receipts").glob("*.json"))), 1)

    def test_http_rejects_oversized_declared_request_without_reading_body(self) -> None:
        status, _ = self.request(
            b"{}",
            headers={"Content-Type": "application/json", "Content-Length": str(adapter.MAX_REQUEST_BYTES + 1)},
        )
        self.assertEqual(status, 413)


class SourcePolicyTests(unittest.TestCase):
    def test_adapter_uses_no_content_digest_implementation(self) -> None:
        text = ADAPTER_PATH.read_text(encoding="utf-8").lower()
        for forbidden in ("hash" + "lib", "sha" + "256", "sha" + "-256"):
            self.assertNotIn(forbidden, text)

    def test_non_loopback_listen_is_rejected(self) -> None:
        self.assertFalse(adapter._is_loopback("0.0.0.0"))
        self.assertTrue(adapter._is_loopback("127.0.0.1"))


if __name__ == "__main__":
    unittest.main()
