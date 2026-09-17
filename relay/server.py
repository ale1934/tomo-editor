#!/usr/bin/env python3
"""Tomo relay server: a tiny shared mailbox between the Tomo editor and Tomo.

Endpoints (all JSON, all require the X-Tomo-Secret header except /health):

    GET  /health              -> {"ok": true}
    POST /push                -> {"ok": true, "id": 42}
        body: {"from": "editor"|"tomo", "kind": "request"|"response"|"note",
               "body": {...}, "in_reply_to": <id>|null}
    GET  /poll?since=<id>      -> {"ok": true, "messages": [...]}

Only the Python standard library is used. Messages are appended to a
JSONL file so a restart doesn't lose them.

    TOMO_RELAY_SECRET=... python3 relay/server.py --port 8472
"""

import argparse
import hmac
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

MAX_MESSAGES = 2000


class Store:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        self.messages = []
        self.next_id = 1
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line)
                    except ValueError:
                        continue
                    self.messages.append(msg)
                    self.next_id = max(self.next_id, msg.get("id", 0) + 1)
            self.messages = self.messages[-MAX_MESSAGES:]
        else:
            os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)

    def push(self, msg):
        with self.lock:
            msg = dict(msg)
            msg["id"] = self.next_id
            msg["ts"] = time.time()
            self.next_id += 1
            self.messages.append(msg)
            self.messages = self.messages[-MAX_MESSAGES:]
            with open(self.path, "a", encoding="utf-8") as f:
                f.write(json.dumps(msg) + "\n")
            return msg["id"]

    def since(self, last_id):
        with self.lock:
            return [m for m in self.messages if m["id"] > last_id]


def make_handler(store, secret):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, code, obj):
            body = json.dumps(obj).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _authorized(self):
            given = self.headers.get("X-Tomo-Secret", "")
            return hmac.compare_digest(given.encode(), secret.encode())

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/health":
                self._send(200, {"ok": True})
                return
            if parsed.path == "/poll":
                if not self._authorized():
                    self._send(403, {"ok": False, "error": "bad secret"})
                    return
                try:
                    since = int(parse_qs(parsed.query).get("since", ["0"])[0])
                except ValueError:
                    since = 0
                self._send(200, {"ok": True,
                                 "messages": store.since(since)})
                return
            self._send(404, {"ok": False, "error": "not found"})

        def do_POST(self):
            parsed = urlparse(self.path)
            if parsed.path != "/push":
                self._send(404, {"ok": False, "error": "not found"})
                return
            if not self._authorized():
                self._send(403, {"ok": False, "error": "bad secret"})
                return
            try:
                length = int(self.headers.get("Content-Length", 0))
            except ValueError:
                length = 0
            try:
                payload = json.loads(self.rfile.read(length).decode("utf-8"))
            except ValueError:
                self._send(400, {"ok": False, "error": "invalid JSON"})
                return
            if payload.get("from") not in ("editor", "tomo"):
                self._send(400, {"ok": False,
                                 "error": 'from must be "editor" or "tomo"'})
                return
            if payload.get("kind") not in ("request", "response", "note"):
                self._send(400, {"ok": False, "error": "bad kind"})
                return
            if not isinstance(payload.get("body"), dict):
                self._send(400, {"ok": False,
                                 "error": "body must be an object"})
                return
            msg_id = store.push({
                "from": payload["from"],
                "kind": payload["kind"],
                "body": payload["body"],
                "in_reply_to": payload.get("in_reply_to"),
            })
            self._send(200, {"ok": True, "id": msg_id})

        def log_message(self, *args):
            sys.stderr.write("[relay] %s\n" % args[0] % args[1:])

    return Handler


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8472)
    ap.add_argument("--data-dir", default="relay-data")
    args = ap.parse_args()

    secret = os.environ.get("TOMO_RELAY_SECRET", "")
    if not secret:
        print("TOMO_RELAY_SECRET is not set", file=sys.stderr)
        sys.exit(1)

    store = Store(os.path.join(args.data_dir, "messages.jsonl"))
    server = ThreadingHTTPServer(("0.0.0.0", args.port),
                                 make_handler(store, secret))
    print(f"[relay] listening on port {args.port} "
          f"({len(store.messages)} messages loaded)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
