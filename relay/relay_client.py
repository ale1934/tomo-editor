#!/usr/bin/env python3
"""Editor-side client for the Tomo relay (see server.py).

The C++ editor shells out to this script; only the standard library is used.

    export TOMO_RELAY_URL="https://<your-relay>"
    export TOMO_RELAY_SECRET="<shared secret>"

    python3 relay/relay_client.py send <request.json>
        request.json: {"kind": "request"|"note", "body": {...}}
        POSTs it as from="editor", prints the new message id, and remembers
        the id so later replies can be matched.

    python3 relay/relay_client.py poll <out.json>
        Fetches new messages, picks the most recent response addressed to
        one of our outstanding requests, and writes:
            {"count": <outstanding replies>, "latest": {...}|null}
        to out.json. Each poll consumes at most one reply; poll again for
        the next.

State (cursor + outstanding request ids) lives in relay/.tomo_* files.
"""

import json
import os
import sys
import urllib.request
import urllib.error
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
CURSOR_PATH = os.path.join(HERE, ".tomo_cursor")
PENDING_PATH = os.path.join(HERE, ".tomo_pending")


def fail(msg):
    print(f"relay_client: {msg}", file=sys.stderr)
    sys.exit(1)


def config():
    url = os.environ.get("TOMO_RELAY_URL", "").rstrip("/")
    secret = os.environ.get("TOMO_RELAY_SECRET", "")
    if not url:
        fail("TOMO_RELAY_URL is not set (see relay/README.md)")
    if not secret:
        fail("TOMO_RELAY_SECRET is not set (see relay/README.md)")
    return url, secret


def api(url, secret, method, path, payload=None):
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(url + path, data=data, method=method,
                                 headers={"Content-Type": "application/json",
                                          "X-Tomo-Secret": secret})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        if e.code == 403:
            fail("relay rejected the secret (wrong TOMO_RELAY_SECRET?)")
        fail(f"relay HTTP {e.code}")
    except urllib.error.URLError as e:
        fail(f"could not reach relay at {url}: {e.reason}")
    except Exception as e:
        fail(str(e))


def load_cursor():
    try:
        return int(open(CURSOR_PATH).read().strip())
    except (OSError, ValueError):
        return 0


def save_cursor(n):
    with open(CURSOR_PATH, "w") as f:
        f.write(str(n))


def load_pending():
    try:
        ids = json.load(open(PENDING_PATH))
        return [int(i) for i in ids] if isinstance(ids, list) else []
    except (OSError, ValueError):
        return []


def save_pending(ids):
    with open(PENDING_PATH, "w") as f:
        json.dump(ids, f)


def cmd_send(request_path):
    url, secret = config()
    try:
        with open(request_path, encoding="utf-8") as f:
            req = json.load(f)
    except (OSError, ValueError) as e:
        fail(f"could not read {request_path}: {e}")
    if req.get("kind") not in ("request", "note") or \
            not isinstance(req.get("body"), dict):
        fail("request.json needs {kind: request|note, body: {...}}")
    resp = api(url, secret, "POST", "/push",
               {"from": "editor", "kind": req["kind"], "body": req["body"],
                "in_reply_to": req.get("in_reply_to")})
    if not resp.get("ok"):
        fail(resp.get("error", "push failed"))
    pending = load_pending()
    pending.append(resp["id"])
    save_pending(pending)
    print(resp["id"])


def cmd_poll(out_path):
    url, secret = config()
    cursor = load_cursor()
    resp = api(url, secret, "GET", f"/poll?since={cursor}")
    if not resp.get("ok"):
        fail(resp.get("error", "poll failed"))
    messages = resp.get("messages", [])
    if messages:
        save_cursor(max(m["id"] for m in messages))

    pending = load_pending()
    replies = [m for m in messages
               if m.get("from") == "tomo" and m.get("kind") == "response"
               and m.get("in_reply_to") in pending]
    latest = max(replies, key=lambda m: m["id"]) if replies else None
    out = {"count": len(replies), "in_reply_to": None,
           "message": None, "new_code": None}
    if latest:
        pending = [i for i in pending if i != latest["in_reply_to"]]
        save_pending(pending)
        body = latest.get("body", {}) or {}
        out["in_reply_to"] = str(latest["in_reply_to"])
        out["message"] = body.get("message")
        out["new_code"] = body.get("new_code")

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f)
    print(len(replies))


def main():
    if len(sys.argv) < 2:
        fail("usage: relay_client.py send <request.json> | poll <out.json>")
    if sys.argv[1] == "send" and len(sys.argv) == 3:
        cmd_send(sys.argv[2])
    elif sys.argv[1] == "poll" and len(sys.argv) == 3:
        cmd_poll(sys.argv[2])
    else:
        fail("usage: relay_client.py send <request.json> | poll <out.json>")


if __name__ == "__main__":
    main()
