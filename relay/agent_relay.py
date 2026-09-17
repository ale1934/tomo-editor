#!/usr/bin/env python3
"""Tomo-side client for the relay (see server.py).

This is what Tomo's own machine runs on a schedule. Only the standard
library is used.

    export TOMO_RELAY_URL="https://<your-relay>"
    export TOMO_RELAY_SECRET="<shared secret>"

    python3 relay/agent_relay.py poll
        Prints a JSON array of new messages from the editor
        (requests and notes) and advances the saved cursor.

    python3 relay/agent_relay.py respond <response.json>
        response.json: {"in_reply_to": <id>, "message": "...",
                        "new_code": "<full file>"|null}
        POSTs it as from="tomo", kind="response".

The cursor lives in relay/.tomo_agent_cursor.
"""

import json
import os
import sys
import urllib.request
import urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
CURSOR_PATH = os.path.join(HERE, ".tomo_agent_cursor")


def fail(msg):
    print(f"agent_relay: {msg}", file=sys.stderr)
    sys.exit(1)


def config():
    url = os.environ.get("TOMO_RELAY_URL", "").rstrip("/")
    secret = os.environ.get("TOMO_RELAY_SECRET", "")
    if not url:
        fail("TOMO_RELAY_URL is not set")
    if not secret:
        fail("TOMO_RELAY_SECRET is not set")
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
        fail(f"relay HTTP {e.code}")
    except urllib.error.URLError as e:
        fail(f"could not reach relay: {e.reason}")
    except Exception as e:
        fail(str(e))


def load_cursor():
    try:
        return int(open(CURSOR_PATH).read().strip())
    except (OSError, ValueError):
        return 0


def cmd_poll():
    url, secret = config()
    cursor = load_cursor()
    resp = api(url, secret, "GET", f"/poll?since={cursor}")
    if not resp.get("ok"):
        fail(resp.get("error", "poll failed"))
    messages = resp.get("messages", [])
    if messages:
        with open(CURSOR_PATH, "w") as f:
            f.write(str(max(m["id"] for m in messages)))
    mine = [{"id": m["id"], "kind": m["kind"], "body": m["body"],
             "in_reply_to": m.get("in_reply_to")}
            for m in messages if m.get("from") == "editor"]
    print(json.dumps(mine))


def cmd_respond(response_path):
    url, secret = config()
    try:
        with open(response_path, encoding="utf-8") as f:
            r = json.load(f)
    except (OSError, ValueError) as e:
        fail(f"could not read {response_path}: {e}")
    if not isinstance(r.get("in_reply_to"), int) or \
            not isinstance(r.get("message"), str):
        fail("response.json needs {in_reply_to: <id>, message: str, "
             "new_code: str|null}")
    resp = api(url, secret, "POST", "/push",
               {"from": "tomo", "kind": "response",
                "body": {"message": r["message"],
                         "new_code": r.get("new_code")},
                "in_reply_to": r["in_reply_to"]})
    if not resp.get("ok"):
        fail(resp.get("error", "push failed"))
    print(resp["id"])


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "poll":
        cmd_poll()
    elif len(sys.argv) == 3 and sys.argv[1] == "respond":
        cmd_respond(sys.argv[2])
    else:
        fail("usage: agent_relay.py poll | respond <response.json>")


if __name__ == "__main__":
    main()
